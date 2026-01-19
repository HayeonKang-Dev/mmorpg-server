#include "LogicManager.h"

std::stack<Job*> JobPool::m_pool;
std::mutex JobPool::m_poolMutex;