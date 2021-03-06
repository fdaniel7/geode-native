/*
 * Licensed to the Apache Software Foundation (ASF) under one or more
 * contributor license agreements.  See the NOTICE file distributed with
 * this work for additional information regarding copyright ownership.
 * The ASF licenses this file to You under the Apache License, Version 2.0
 * (the "License"); you may not use this file except in compliance with
 * the License.  You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */
#include <geode/geode_globals.hpp>
#include <geode/geode_types.hpp>
#include "ExecutionImpl.hpp"
#include <geode/ExceptionTypes.hpp>
#include "ThinClientRegion.hpp"
#include "ThinClientPoolDM.hpp"
#include "NoResult.hpp"
#include "UserAttributes.hpp"
// using namespace apache::geode::client;

FunctionToFunctionAttributes ExecutionImpl::m_func_attrs;
ACE_Recursive_Thread_Mutex ExecutionImpl::m_func_attrs_lock;
ExecutionPtr ExecutionImpl::withFilter(CacheableVectorPtr routingObj) {
  // ACE_Guard<ACE_Recursive_Thread_Mutex> _guard(m_lock);
  if (routingObj == nullptr) {
    throw IllegalArgumentException("Execution::withFilter: filter is null");
  }
  if (m_region == nullptr) {
    throw UnsupportedOperationException(
        "Execution::withFilter: FunctionService::onRegion needs to be called "
        "first before calling this function");
  }
  //      m_routingObj = routingObj;
  return std::make_shared<ExecutionImpl>(routingObj, m_args, m_rc, m_region,
                                         m_allServer, m_pool, m_proxyCache);
}
ExecutionPtr ExecutionImpl::withArgs(CacheablePtr args) {
  // ACE_Guard<ACE_Recursive_Thread_Mutex> _guard(m_lock);
  if (args == nullptr) {
    throw IllegalArgumentException("Execution::withArgs: args is null");
  }
  //  m_args = args;
  return std::make_shared<ExecutionImpl>(m_routingObj, args, m_rc, m_region,
                                         m_allServer, m_pool, m_proxyCache);
}
ExecutionPtr ExecutionImpl::withCollector(ResultCollectorPtr rs) {
  // ACE_Guard<ACE_Recursive_Thread_Mutex> _guard(m_lock);
  if (rs == nullptr) {
    throw IllegalArgumentException(
        "Execution::withCollector: collector is null");
  }
  //	m_rc = rs;
  return std::make_shared<ExecutionImpl>(m_routingObj, m_args, rs, m_region,
                                         m_allServer, m_pool, m_proxyCache);
}

std::vector<int8_t>* ExecutionImpl::getFunctionAttributes(const char* func) {
  std::map<std::string, std::vector<int8_t>*>::iterator itr =
      m_func_attrs.find(func);
  if (itr != m_func_attrs.end()) {
    return itr->second;
  }
  return nullptr;
}

ResultCollectorPtr ExecutionImpl::execute(CacheableVectorPtr& routingObj,
                                          CacheablePtr& args,
                                          ResultCollectorPtr& rs,
                                          const char* func, uint32_t timeout) {
  m_routingObj = routingObj;
  m_args = args;
  m_rc = rs;
  return execute(func, timeout);
}

ResultCollectorPtr ExecutionImpl::execute(const char* fn, uint32_t timeout) {
  std::string func = fn;
  LOGDEBUG("ExecutionImpl::execute: ");
  GuardUserAttribures gua;
  if (m_proxyCache != nullptr) {
    LOGDEBUG("ExecutionImpl::execute function on proxy cache");
    gua.setProxyCache(m_proxyCache);
  }
  bool serverHasResult = false;
  bool serverIsHA = false;
  bool serverOptimizeForWrite = false;

  std::vector<int8_t>* attr = getFunctionAttributes(fn);
  {
    if (attr == nullptr) {
      ACE_Guard<ACE_Recursive_Thread_Mutex> _guard(m_func_attrs_lock);
      GfErrType err = GF_NOERR;
      attr = getFunctionAttributes(fn);
      if (attr == nullptr) {
        if (m_region != nullptr) {
          err = dynamic_cast<ThinClientRegion*>(m_region.get())
                    ->getFuncAttributes(fn, &attr);
        } else if (m_pool != nullptr) {
          err = getFuncAttributes(fn, &attr);
        }
        if (err != GF_NOERR) {
          GfErrTypeToException("Execute::GET_FUNCTION_ATTRIBUTES", err);
        }
        if (!attr->empty() && err == GF_NOERR) {
          m_func_attrs[fn] = attr;
        }
      }
    }
  }
  serverHasResult = ((attr->at(0) == 1) ? true : false);
  serverIsHA = ((attr->at(1) == 1) ? true : false);
  serverOptimizeForWrite = ((attr->at(2) == 1) ? true : false);

  LOGDEBUG(
      "ExecutionImpl::execute got functionAttributes from srver for function = "
      "%s serverHasResult = %d "
      " serverIsHA = %d serverOptimizeForWrite = %d ",
      func.c_str(), serverHasResult, serverIsHA, serverOptimizeForWrite);

  if (serverHasResult == false) {
    m_rc = std::make_shared<NoResult>();
  } else if (m_rc == nullptr) {
    m_rc = std::make_shared<ResultCollector>();
  }

  uint8_t isHAHasResultOptimizeForWrite = 0;
  if (serverIsHA) {
    isHAHasResultOptimizeForWrite = isHAHasResultOptimizeForWrite | 1;
  }

  if (serverHasResult) {
    isHAHasResultOptimizeForWrite = isHAHasResultOptimizeForWrite | 2;
  }

  if (serverOptimizeForWrite) {
    isHAHasResultOptimizeForWrite = isHAHasResultOptimizeForWrite | 4;
  }

  LOGDEBUG("ExecutionImpl::execute: isHAHasResultOptimizeForWrite = %d",
           isHAHasResultOptimizeForWrite);
  TXState* txState = TSSTXStateWrapper::s_geodeTSSTXState->getTXState();

  if (txState != nullptr && m_allServer == true) {
    throw UnsupportedOperationException(
        "Execution::execute: Transaction function execution on all servers is "
        "not supported");
  }

  if (m_region != nullptr) {
    int32_t retryAttempts = 3;
    if (m_pool != nullptr) {
      retryAttempts = m_pool->getRetryAttempts();
    }

    //    if(txState != nullptr && !txState->isReplay())
    //    {
    //		auto args = std::make_shared<VectorOfCacheable>();
    //		args->push_back(m_args);
    //		args->push_back(m_routingObj);
    //		args->push_back(m_rc);
    //		args->push_back(CacheableString::create(func));
    //		args->push_back(CacheableInt32::create(timeout));
    //		txState->recordTXOperation(GF_EXECUTE_FUNCTION,
    // m_region==nullptr?nullptr:m_region->getFullPath(), nullptr, args);
    //    }
    //    try{
    if (m_pool != nullptr && m_pool->getPRSingleHopEnabled()) {
      auto tcrdm = std::dynamic_pointer_cast<ThinClientPoolDM>(m_pool);
      if (!tcrdm) {
        throw IllegalArgumentException(
            "Execute: pool cast to ThinClientPoolDM failed");
      }
      auto cms = tcrdm->getClientMetaDataService();
      auto failedNodes = CacheableHashSet::create();
      if ((!m_routingObj || m_routingObj->empty()) &&
          txState == nullptr) {  // For transactions we should not create
                                 // multiple threads
        LOGDEBUG("ExecutionImpl::execute: m_routingObj is empty");
        auto serverToBucketsMap = cms->groupByServerToAllBuckets(
            m_region,
            /*serverOptimizeForWrite*/ (isHAHasResultOptimizeForWrite & 4));
        if (!serverToBucketsMap || serverToBucketsMap->empty()) {
          LOGDEBUG(
              "ExecutionImpl::execute: m_routingObj is empty and locationMap "
              "is also empty so use old FE onRegion");
          std::dynamic_pointer_cast<ThinClientRegion>(m_region)
              ->executeFunction(
                  fn, m_args, m_routingObj, isHAHasResultOptimizeForWrite, m_rc,
                  (isHAHasResultOptimizeForWrite & 1) ? retryAttempts : 0,
                  timeout);
          cms->enqueueForMetadataRefresh(m_region->getFullPath(), 0);
        } else {
          // convert server to bucket map to server to key map where bucket id
          // is key.
          auto serverToKeysMap =
              std::make_shared<ClientMetadataService::ServerToKeysMap>(
                  serverToBucketsMap->size());
          for (const auto& entry : *serverToBucketsMap) {
            auto keys =
                std::make_shared<CacheableHashSet>(entry.second->size());
            for (const auto& bucket : *(entry.second)) {
              keys->insert(CacheableInt32::create(bucket));
            }
            serverToKeysMap->emplace(entry.first, keys);
          }
          LOGDEBUG(
              "ExecutionImpl::execute: withoutFilter and locationMap is not "
              "empty");
          bool reExecute = std::dynamic_pointer_cast<ThinClientRegion>(m_region)
                               ->executeFunctionSH(
                                   fn, m_args, isHAHasResultOptimizeForWrite,
                                   m_rc, serverToKeysMap, failedNodes, timeout,
                                   /*allBuckets*/ true);
          if (reExecute) {  // Fallback to old FE onREgion
            if (isHAHasResultOptimizeForWrite & 1) {  // isHA = true
              m_rc->clearResults();
              CacheableVectorPtr rs =
                  std::dynamic_pointer_cast<ThinClientRegion>(m_region)
                      ->reExecuteFunction(fn, m_args, m_routingObj,
                                          isHAHasResultOptimizeForWrite, m_rc,
                                          (isHAHasResultOptimizeForWrite & 1)
                                              ? retryAttempts
                                              : 0,
                                          failedNodes, timeout);
            } else {  // isHA = false
              m_rc->clearResults();
              dynamic_cast<ThinClientRegion*>(m_region.get())
                  ->executeFunction(
                      fn, m_args, m_routingObj, isHAHasResultOptimizeForWrite,
                      m_rc,
                      (isHAHasResultOptimizeForWrite & 1) ? retryAttempts : 0,
                      timeout);
            }
          }
        }
      } else if (m_routingObj != nullptr && m_routingObj->size() == 1) {
        LOGDEBUG("executeFunction onRegion WithFilter size equal to 1 ");
        dynamic_cast<ThinClientRegion*>(m_region.get())
            ->executeFunction(
                fn, m_args, m_routingObj, isHAHasResultOptimizeForWrite, m_rc,
                (isHAHasResultOptimizeForWrite & 1) ? retryAttempts : 0,
                timeout);
      } else {
        if (txState == nullptr) {
          auto serverToKeysMap = cms->getServerToFilterMapFESHOP(
              m_routingObj, m_region, /*serverOptimizeForWrite*/
              (isHAHasResultOptimizeForWrite & 4));
          if (!serverToKeysMap || serverToKeysMap->empty()) {
            LOGDEBUG(
                "ExecutionImpl::execute: withFilter but locationMap is empty "
                "so use old FE onRegion");
            dynamic_cast<ThinClientRegion*>(m_region.get())
                ->executeFunction(
                    fn, m_args, m_routingObj, isHAHasResultOptimizeForWrite,
                    m_rc,
                    (isHAHasResultOptimizeForWrite & 1) ? retryAttempts : 0,
                    timeout);
            cms->enqueueForMetadataRefresh(m_region->getFullPath(), 0);
          } else {
            LOGDEBUG(
                "ExecutionImpl::execute: withFilter and locationMap is not "
                "empty");
            bool reExecute =
                dynamic_cast<ThinClientRegion*>(m_region.get())
                    ->executeFunctionSH(fn, m_args,
                                        isHAHasResultOptimizeForWrite, m_rc,
                                        serverToKeysMap, failedNodes, timeout,
                                        /*allBuckets*/ false);
            if (reExecute) {  // Fallback to old FE onREgion
              if (isHAHasResultOptimizeForWrite & 1) {  // isHA = true
                m_rc->clearResults();
                CacheableVectorPtr rs =
                    dynamic_cast<ThinClientRegion*>(m_region.get())
                        ->reExecuteFunction(fn, m_args, m_routingObj,
                                            isHAHasResultOptimizeForWrite, m_rc,
                                            (isHAHasResultOptimizeForWrite & 1)
                                                ? retryAttempts
                                                : 0,
                                            failedNodes, timeout);
              } else {  // isHA = false
                m_rc->clearResults();
                dynamic_cast<ThinClientRegion*>(m_region.get())
                    ->executeFunction(
                        fn, m_args, m_routingObj, isHAHasResultOptimizeForWrite,
                        m_rc,
                        (isHAHasResultOptimizeForWrite & 1) ? retryAttempts : 0,
                        timeout);
              }
            }
          }
        } else {  // For transactions use old way
          dynamic_cast<ThinClientRegion*>(m_region.get())
              ->executeFunction(
                  fn, m_args, m_routingObj, isHAHasResultOptimizeForWrite, m_rc,
                  (isHAHasResultOptimizeForWrite & 1) ? retryAttempts : 0,
                  timeout);
        }
      }
    } else {  // w/o single hop, Fallback to old FE onREgion
      dynamic_cast<ThinClientRegion*>(m_region.get())
          ->executeFunction(
              fn, m_args, m_routingObj, isHAHasResultOptimizeForWrite, m_rc,
              (isHAHasResultOptimizeForWrite & 1) ? retryAttempts : 0, timeout);
    }
    /*    } catch (TransactionDataNodeHasDepartedException e) {
                    if(txState == nullptr)
                    {
                            GfErrTypeThrowException("Transaction is nullptr",
       GF_CACHE_ILLEGAL_STATE_EXCEPTION);
                    }

                    if(!txState->isReplay())
                            txState->replay(false);
            } catch(TransactionDataRebalancedException e) {
                    if(txState == nullptr)
                    {
                            GfErrTypeThrowException("Transaction is nullptr",
       GF_CACHE_ILLEGAL_STATE_EXCEPTION);
                    }

                    if(!txState->isReplay())
                            txState->replay(true);
            }
    */
    if (serverHasResult == true) {
      // ExecutionImpl::addResults(m_rc, rs);
      m_rc->endResults();
    }

    return m_rc;
  } else if (m_pool != nullptr) {
    if (txState != nullptr) {
      throw UnsupportedOperationException(
          "Execution::execute: Transaction function execution on pool is not "
          "supported");
    }
    if (m_allServer == false) {
      executeOnPool(
          func, isHAHasResultOptimizeForWrite,
          (isHAHasResultOptimizeForWrite & 1) ? m_pool->getRetryAttempts() : 0,
          timeout);
      if (serverHasResult == true) {
        // ExecutionImpl::addResults(m_rc, rs);
        m_rc->endResults();
      }
      return m_rc;
    }
    executeOnAllServers(func, isHAHasResultOptimizeForWrite, timeout);
  } else {
    throw IllegalStateException("Execution::execute: should not be here");
  }
  return m_rc;
}

GfErrType ExecutionImpl::getFuncAttributes(const char* func,
                                           std::vector<int8_t>** attr) {
  ThinClientPoolDM* tcrdm = dynamic_cast<ThinClientPoolDM*>(m_pool.get());
  if (tcrdm == nullptr) {
    throw IllegalArgumentException(
        "Execute: pool cast to ThinClientPoolDM failed");
  }

  GfErrType err = GF_NOERR;

  // do TCR GET_FUNCTION_ATTRIBUTES
  LOGDEBUG("Tcrmessage request GET_FUNCTION_ATTRIBUTES ");
  std::string funcName(func);
  TcrMessageGetFunctionAttributes request(funcName, tcrdm);
  TcrMessageReply reply(true, tcrdm);
  err = tcrdm->sendSyncRequest(request, reply);
  if (err != GF_NOERR) {
    return err;
  }
  switch (reply.getMessageType()) {
    case TcrMessage::RESPONSE: {
      *attr = reply.getFunctionAttributes();
      break;
    }
    case TcrMessage::EXCEPTION: {
      err = dynamic_cast<ThinClientRegion*>(m_region.get())
                ->handleServerException("Region::GET_FUNCTION_ATTRIBUTES",
                                        reply.getException());
      break;
    }
    default: {
      LOGERROR("Unknown message type %d while getting function attributes.",
               reply.getMessageType());
      err = GF_MSG;
    }
  }
  return err;
}

void ExecutionImpl::addResults(ResultCollectorPtr& collector,
                               const CacheableVectorPtr& results) {
  if (results == nullptr || collector == nullptr) {
    return;
  }

  for (int32_t resultItem = 0; resultItem < results->size(); resultItem++) {
    CacheablePtr result(results->operator[](resultItem));
    collector->addResult(result);
  }
}
void ExecutionImpl::executeOnAllServers(std::string& func, uint8_t getResult,
                                        uint32_t timeout) {
  ThinClientPoolDM* tcrdm = dynamic_cast<ThinClientPoolDM*>(m_pool.get());
  if (tcrdm == nullptr) {
    throw IllegalArgumentException(
        "Execute: pool cast to ThinClientPoolDM failed");
  }
  CacheableStringPtr exceptionPtr = nullptr;
  GfErrType err = tcrdm->sendRequestToAllServers(
      func.c_str(), getResult, timeout, m_args, m_rc, exceptionPtr);
  if (exceptionPtr != nullptr && err != GF_NOERR) {
    LOGDEBUG("Execute errorred: %d", err);
    // throw FunctionExecutionException( "Execute: failed to execute function
    // with server." );
    if (err == GF_CACHESERVER_EXCEPTION) {
      throw FunctionExecutionException(
          "Execute: failed to execute function with server.");
    } else {
      GfErrTypeToException("Execute", err);
    }
  }

  if (err == GF_AUTHENTICATION_FAILED_EXCEPTION ||
      err == GF_NOT_AUTHORIZED_EXCEPTION ||
      err == GF_AUTHENTICATION_REQUIRED_EXCEPTION) {
    GfErrTypeToException("Execute", err);
  }

  if (err != GF_NOERR) {
    if (err == GF_CACHESERVER_EXCEPTION) {
      throw FunctionExecutionException(
          "Execute: exception "
          "at the server side: ",
          exceptionPtr->asChar());
    } else {
      LOGDEBUG("Execute errorred with server exception: %d", err);
      throw FunctionExecutionException(
          "Execute: failed to execute function on servers.");
    }
  }
}
CacheableVectorPtr ExecutionImpl::executeOnPool(std::string& func,
                                                uint8_t getResult,
                                                int32_t retryAttempts,
                                                uint32_t timeout) {
  ThinClientPoolDM* tcrdm = dynamic_cast<ThinClientPoolDM*>(m_pool.get());
  if (tcrdm == nullptr) {
    throw IllegalArgumentException(
        "Execute: pool cast to ThinClientPoolDM failed");
  }
  int32_t attempt = 0;

  // CacheableStringArrayPtr csArray = tcrdm->getServers();

  // if (csArray != nullptr && csArray->length() != 0) {
  //  for (int i = 0; i < csArray->length(); i++)
  //  {
  //    CacheableStringPtr cs = csArray[i];
  //    TcrEndpoint *ep = nullptr;
  //    /*
  //    std::string endpointStr = Utils::convertHostToCanonicalForm(cs->asChar()
  //    );
  //    */
  //    ep = tcrdm->addEP(cs->asChar());
  //  }
  //}

  // if pools retry attempts are not set then retry once on all available
  // endpoints
  if (retryAttempts == -1) {
    retryAttempts = static_cast<int32_t>(tcrdm->getNumberOfEndPoints());
  }

  while (attempt <= retryAttempts) {
    std::string funcName(func);
    TcrMessageExecuteFunction msg(funcName, m_args, getResult, tcrdm, timeout);
    TcrMessageReply reply(true, tcrdm);
    ChunkedFunctionExecutionResponse* resultCollector(
        new ChunkedFunctionExecutionResponse(reply, (getResult & 2), m_rc));
    reply.setChunkedResultHandler(resultCollector);
    reply.setTimeout(timeout);

    GfErrType err = GF_NOERR;
    err = tcrdm->sendSyncRequest(msg, reply, !(getResult & 1));
    LOGFINE("executeOnPool %d attempt = %d retryAttempts = %d", err, attempt,
            retryAttempts);
    if (err == GF_NOERR &&
        (reply.getMessageType() == TcrMessage::EXCEPTION ||
         reply.getMessageType() == TcrMessage::EXECUTE_FUNCTION_ERROR)) {
      err = ThinClientRegion::handleServerException("Execute",
                                                    reply.getException());
    }
    if (ThinClientBaseDM::isFatalClientError(err)) {
      GfErrTypeToException("ExecuteOnPool:", err);
    } else if (err != GF_NOERR) {
      if (getResult & 1) {
        resultCollector->reset();
        m_rc->clearResults();
        attempt++;
        if (attempt > 0) {
          getResult |= 8;  // Send this on server, so that it can identify that
                           // it is a retry attempt.
        }
        continue;
      } else {
        GfErrTypeToException("ExecuteOnPool:", err);
      }
    }
    // CacheableVectorPtr values =
    // resultCollector->getFunctionExecutionResults();
    /*
    ==25848== 1,610 (72 direct, 1,538 indirect) bytes in 2 blocks are definitely
    lost in loss record 193 of 218
    ==25848==    at 0x4007D75: operator new(unsigned int)
    (vg_replace_malloc.c:313)
    ==25848==    by 0x42B575A:
    apache::geode::client::ExecutionImpl::executeOnPool(stlp_std::basic_string<char,
    stlp_std::char_traits<char>, stlp_std::allocator<char> >&, unsigned char,
    int, unsigned int) (ExecutionImpl.cpp:426)
    ==25848==    by 0x42B65E6:
    apache::geode::client::ExecutionImpl::execute(char const*,
    bool, unsigned int, bool, bool, bool) (ExecutionImpl.cpp:292)
    ==25848==    by 0x42B7897:
    apache::geode::client::ExecutionImpl::execute(char const*,
    bool, unsigned int, bool, bool) (ExecutionImpl.cpp:76)
    ==25848==    by 0x8081320: Task_Client1OpTest::doTask() (in
    /export/pnq-gst-dev01a/users/adongre/cedar_dev_Nov12/build-artifacts/linux/tests/cppcache/testThinClientPoolExecuteFunctionPrSHOP)
    ==25848==    by 0x808D5CA: dunit::TestSlave::begin() (in
    /export/pnq-gst-dev01a/users/adongre/cedar_dev_Nov12/build-artifacts/linux/tests/cppcache/testThinClientPoolExecuteFunctionPrSHOP)
    ==25848==    by 0x8089807: dunit::dmain(int, char**) (in
    /export/pnq-gst-dev01a/users/adongre/cedar_dev_Nov12/build-artifacts/linux/tests/cppcache/testThinClientPoolExecuteFunctionPrSHOP)
    ==25848==    by 0x805F9EA: main (in
    /export/pnq-gst-dev01a/users/adongre/cedar_dev_Nov12/build-artifacts/linux/tests/cppcache/testThinClientPoolExecuteFunctionPrSHOP)
    */
    delete resultCollector;
    resultCollector = nullptr;

    return nullptr;
  }
  return nullptr;
}
