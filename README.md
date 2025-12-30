# 🚀 IOCP 기반 고성능 게임 서버 엔진

C++와 Windows IOCP(Input/Output Completion Port) API를 활용하여 제작 중인 비동기 네트워크 서버 엔진입니다. 고성능, 고가용성을 목표로 세션 풀링과 효율적인 메모리 관리를 지원합니다.

---

## 🛠 현재 구현된 기능

### 1. Network Core
- **IocpCore**: IOCP 커널 객체 관리 및 Worker Thread를 위한 이벤트 디스패처(`Dispatch`) 구현.
- **Listener**: `AcceptEx`를 활용한 비동기 접속 대기 로직 구현.

### 2. Memory & Buffer Management
- **RingBuffer**: 데이터 복사를 최소화하고 Wrap-around를 지원하는 순환 수신 버퍼.
- **SessionManager**: 세션 객체 재사용을 통한 메모리 단편화 방지 및 성능 최적화 (Session Pooling).

### 3. Session Logic
- **Session**: 소켓 관리, `WSAOVERLAPPED` 구조체를 이용한 비동기 IO 상태 관리 및 초기화 로직 구현.

---

## 📂 폴더 구조 (Directory Structure)

```text
/Server
  ├── /Core           # 서버 엔진 핵심 로직 (IocpCore 등)
  ├── /Network        # 네트워크 통신 모듈
  │    ├── /Include   # 헤더 파일 (.h)
  │    └── /Source    # 구현 파일 (.cpp)
  ├── /Client         # 테스트용 클라이언트
  └── Main.cpp        # 서버 엔트리 포인트

---

🗺 앞으로 구현할 것 (Roadmap)
Phase 1: 기반 완성
[ ] Worker Thread Pool: CPU 코어 수에 최적화된 스레드 생성 및 관리 시스템.

[ ] Packet Marshaling: 바이트 스트림을 패킷 단위로 조립하고 분해하는 로직.

Phase 2: 송수신 심화
[ ] Send Buffer: 효율적인 송신을 위한 Gather Send 및 송신 큐 관리.

[ ] Protobuf/FlatBuffers: 직렬화 라이브러리 연동을 통한 패킷 자동화.

Phase 3: 게임 로직 & 동기화
[ ] Job Queue: 멀티스레드 환경에서 데이터 경합을 최소화하는 일감 처리 시스템.

[ ] Deadlock Detector: 자원 경쟁 상태 모니터링 툴.

Phase 4: 성능 모니터링
[ ] Logging System: 멀티스레드 안전한 비동기 로그 시스템.

[ ] Monitoring UI: 접속자 수 및 서버 상태 실시간 대시보드.
---
💻 실행 방법 (Getting Started)
환경: Windows 10/11, Visual Studio 2022 이상.

빌드: x64 플랫폼 권장.

종속성: ws2_32.lib, mswsock.lib 링크 필요.

프로젝트 설정의 추가 포함 디렉터리에 $(ProjectDir)Network/Include 및 $(ProjectDir)Core 등록 확인.
