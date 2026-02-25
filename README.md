# MMO RPG Server

저자원·고처리량을 목표로 설계한 C++ IOCP 기반 게임 서버 — 1,000 CCU 부하 테스트로 검증

## 핵심 성과

| 지표 | 결과 |
|------|------|
| 동시접속자 (CCU) | **1,000명** (30분 이상 안정 유지) |
| 평균 CPU 사용률 | **7.19%** |
| 최대 메모리 사용량 | **77.3 MB** |
| 중간 거부 / 타임아웃 | **0건** |

---

## 아키텍처

```
                        ┌──────────────┐   ┌───────────────┐
                        │ MPSC Queue×4 │   │  AOI System   │
                        └──────┬───────┘   └───────┬───────┘
                               │ Pop               │
┌────────────────────┐  Push   ▼                   │
│  IOCP Worker ×4    │───────► Logic Thread ×4 ─────┘
│  (비동기 I/O 처리)  │         (Session Affinity)    │
└────────────────────┘                              ▼
         ▲                                    DB Thread ×2
         │                                   (MySQL 비동기)
┌────────┴───────────┐
│  AcceptEx Pool×20  │   [연결 수락]
│  Object Pool       │   Session×1530 / SendBuf×5000 / Job×1000
│  Ring Buffer (8KB) │   [수신 버퍼]
└────────────────────┘

[패킷 처리 흐름]
WSARecv 완료 → Ring Buffer 파싱 → Job 생성 → MPSC Queue Push
→ Logic Thread Pop → HandlePacket → 브로드캐스트 (Batch Send)
```

---

## 핵심 기술

### IOCP 비동기 I/O
- `AcceptEx` 20개 사전 등록 → 연결 완료 즉시 새 슬롯 보충
- `WSARecv` / `WSASend` overlapped 구조로 커널-유저 모드 전환 최소화
- Batch Send: `WSABUF` 배열에 송신 대기 패킷 적재 후 단일 `WSASend` 호출

### Session Affinity
```
thread_idx = (session_ptr >> 6) % 4
```
같은 세션의 Job은 항상 동일 Logic Thread에서 처리 → 세션 내부 Lock 불필요

### MPSC Job Queue
- IOCP Worker(N) → Logic Thread(1) 구조의 다중 생산자 단일 소비자 큐
- `condition_variable`로 Job 없을 때 스레드 Sleep, `PushJob` 시 해당 스레드만 깨움

### Object Pooling

| 풀 | 사전 할당 | 상한 | 용도 |
|----|-----------|------|------|
| Session Pool | 1,530개 | 고정 | 연결 세션 관리 |
| SendBuffer Pool | 5,000개 | 10,000개 | 송신 버퍼 |
| Job Pool | 동적 | 1,000개 | 로직 작업 단위 |

- 풀 소진 시 동적 할당 후 반환 → 상한 초과분 즉시 `delete`
- Job Pool은 스택(LIFO) 구조로 최근 사용 객체의 캐시 히트율 향상

### Ring Buffer
- 8KB 원형 버퍼로 TCP 스트림 경계 처리
- Peek → Read 2단계 파싱: 완성된 패킷만 추출

### Grid AOI
```
MAP 1000×1000 → 100×100 셀 → 10×10 = 100개 그리드
시야 범위 1칸 (3×3 = 9 셀 구독)
```
- 플레이어를 시야 범위 내 9개 그리드에 사전 등록 → O(1) 탐색
- 그리드 경계 이동 시만 델타 업데이트 → 불필요한 전체 재계산 없음
- 1,000 CCU 기준 브로드캐스트 1회 당 평균 약 70명 대상

### Time Wheel
```
50 슬롯 × 100ms/tick
```
- 전체 타이머 리스트 순회 없이 현재 틱 슬롯만 실행 → O(N) → O(1)
- 리스폰(5초), 세션 타임아웃(CONNECTED 10초 / GAME 30초) 처리

### DB Thread 분리
- Logic Thread → `PushQuery(task)` → DB Worker ×2 → 결과 Job → Logic Queue
- DB 블로킹 쿼리를 별도 스레드에서 실행해 Logic 루프 블로킹 방지
- MySQL Connector/C++ 사용, 각 Worker가 독립 Connection 유지

### Backpressure
- Session의 SendQueue 임계치 초과 시 소켓 강제 종료 → 처리 큐 포화 방지

---

## 스레드 구조

| 스레드 | 수 | 역할 |
|--------|-----|------|
| Main | 1 | 초기화, Worker Join |
| IOCP Worker | 4 | `GetQueuedCompletionStatus` 처리 |
| Logic Thread | 4 | 패킷 핸들링, 게임 로직 |
| DB Worker | 2 | MySQL 쿼리 실행 |
| **합계** | **11** | |

> Logic Thread 0만 `World::Update()`, CCU 집계, AOI 로그, 세션 타임아웃, 모니터링 담당

---

## Trouble Shooting: 단일 Logic Thread 병목

**문제**: 초기 단일 Logic Thread 구조에서 1,000 CCU 부하 테스트 시 동일 그리드 플레이어 밀집으로 N² Send 폭발 → SendQueue 포화 → Backpressure 강제 종료 다량 발생

```
500명 밀집: 500 × 500 ≈ 250,000 Send/tick → 단일 스레드 한계 초과
```

**해결**: Session Affinity 기반 Logic Thread 4개로 확장

- `session_ptr` 해시로 Thread 고정 → 세션 내부 Lock 불필요
- 세션 간 병렬 처리로 N² 부하를 4개 Thread가 분담
- **결과**: 처리량 4배 향상, 강제 종료 0건

---

## 파일 구조

```
MMO/Server/
├── Main.cpp
├── Core/
│   ├── IocpCore.h/cpp         # IOCP 커널 객체, Dispatch
│   ├── LogicManager.h         # MPSC Queue, Session Affinity, Job Pool
│   ├── PacketHandler.h/cpp    # 패킷 핸들러 (LOGIN/MOVE/ATTACK/CHAT 등)
│   ├── Protocol.h             # 패킷 구조체 정의
│   └── TimeWheel.h/cpp        # O(1) 타이머
├── Network/
│   ├── Include/
│   │   ├── Session.h          # 세션 상태, Ring Buffer, Send Queue
│   │   ├── SessionManager.h   # Session Pool (Acquire/Release)
│   │   ├── SendBuffer.h       # SendBuffer Pool
│   │   ├── RingBuffer.h       # 8KB 원형 수신 버퍼
│   │   └── Listener.h         # AcceptEx
│   └── Source/
│       ├── Session.cpp
│       ├── SessionManager.cpp
│       ├── SendBuffer.cpp
│       ├── Listener.cpp
│       └── RingBuffer.cpp
├── Game/
│   ├── World.h/cpp            # Grid AOI, Broadcast, EnterGame/LeaveGame
│   └── Player.h/cpp           # 플레이어 상태, Die/Respawn
├── DB/
│   └── DBManager.h/cpp        # MySQL Worker Thread, PushQuery
├── Observability/
│   ├── ServerMonitor.h        # 성능 지표 수집 (CSV 로그)
│   └── AOILogger.h            # AOI 현황 로그
└── PlayerManager.h/cpp
```

---

## 빌드 환경

| 항목 | 버전 |
|------|------|
| 언어 | C++17 |
| OS | Windows 11 |
| IDE | Visual Studio 2022 |
| DB | MySQL 8.x + Connector/C++ |

**의존성**
- `Ws2_32.lib` — Winsock2
- `Psapi.lib` — 메모리 사용량 측정
- `mysqlcppconn.lib` — MySQL Connector/C++

---

## 실행 방법

```
# 포트 8001로 서버 시작
Server.exe

# MySQL 연결 정보 (Main.cpp에서 수정)
DBManager::Get()->Init(2, "tcp://127.0.0.1:3306", "root", "<password>", "mmo_db");
```

---

## 성능 모니터링

서버 실행 시 `server_performance_log.csv` 자동 생성

```
Timestamp, CCU, Recv_PPS, Send_PPS, Memory_MB, CPU_Percent,
IOCP_0, IOCP_1, IOCP_2, IOCP_3, Logic_Jobs, DB_Queries
```
