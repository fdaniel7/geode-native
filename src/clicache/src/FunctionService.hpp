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

#pragma once

#include "geode_defs.hpp"
#include "begin_native.hpp"
#include <geode/FunctionService.hpp>
#include "end_native.hpp"

#include "Cache.hpp"

using namespace System;

namespace Apache
{
  namespace Geode
  {
    namespace Client
    {

    //  generic<class TKey, class TValue>
      ref class Pool;
     
      generic<class TResult>
      ref class Execution;

      /// <summary>
      /// A factory class used to create Execute object for function execution
      /// </summary>
      /// <remarks>
      generic<class TResult>
      public ref class FunctionService
      {
      public:

        /// <summary>
        /// Creates a new region Execution object
        /// </summary>
        /// <remarks>
        /// If Pool is multiusersecure mode then one need to pass logical instance of Region Pool->CreateSecureUserCache(<credentials>)->getRegion(<regionPath>).
        /// </remarks>
        generic <class TKey, class TValue>
        static Execution<TResult>^ OnRegion( IRegion<TKey, TValue>^ rg );

        /// <summary>
        /// Creates a new Execution object on one server
        /// </summary>
        /// <remarks>
        /// </remarks>
        /// <exception cref="UnsupportedOperationException">unsupported operation exception, when Pool is in multiusersecure mode.</exception>
        static Execution<TResult>^ OnServer( Pool^ pl );

        /// <summary>
        /// Creates a new Execution object on all servers in the pool
        /// </summary>
        /// <remarks>
        /// </remarks>
        /// <exception cref="UnsupportedOperationException">unsupported operation exception, when Pool is in multiusersecure mode.</exception>
        static Execution<TResult>^ OnServers( Pool^ pl );

        /// <summary>
        /// Creates a new Execution object on one server.
        /// </summary>
        /// <remarks>
        /// </remarks>
        /// <exception cref="IllegalStateException">when Pool has been closed.</exception>
        static Execution<TResult>^ OnServer( IRegionService^ cache );

        /// <summary>
        /// Creates a new Execution object on all servers in the pool.
        /// </summary>
        /// <remarks>
        /// </remarks>
        /// <exception cref="IllegalStateException">when Pool has been closed.</exception>
        static Execution<TResult>^ OnServers( IRegionService^ cache );
        
      };
    }  // namespace Client
  }  // namespace Geode
}  // namespace Apache

