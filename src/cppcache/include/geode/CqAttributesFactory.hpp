#pragma once

#ifndef GEODE_CQATTRIBUTESFACTORY_H_
#define GEODE_CQATTRIBUTESFACTORY_H_

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

#include "geode_globals.hpp"
#include "geode_types.hpp"
#include "CqAttributes.hpp"
#include "CqListener.hpp"

/**
 * @file
 */

namespace apache {
namespace geode {
namespace client {

/**
 * @class CqAttributesFactory CqAttributesFactory.hpp
 *
 * The factory class for the CqAttributes instance. This provides the CqListener
 * setter methods. This class maintains state for and creates new instances of
 * the
 * CqAttributes interface for new CqQuery instances.
 * If you create a factory with the default constructor, then the factory is set
 * up to create attributes with all default settings. You can also create a
 * factory
 * by providing a <code>CqAttributes</code>, which will set up the new factory
 * with
 * the settings provided in that attributes instance.
 *
 * <p>Once a <code>CqAttributes</code> is created, it can only be modified
 * after it has been used to create a <code>CqQuery</code>.
 *
 * @see CqAttributes
 *
 */
class CPPCACHE_EXPORT CqAttributesFactory  {
 public:
  /**
   * Creates a new instance of AttributesFactory ready to create a
   * <code>CqAttributes</code> with default settings.
   */
  CqAttributesFactory();

  /**
   * Creates a new instance of CqAttributesFactory ready to create a
   * <code>CqAttributes</code> with the same settings as those in the
   * specified <code>CqAttributes</code>.
   *
   * @param cqAttributes
   *          the <code>CqAttributes</code> used to initialize this
   *          AttributesFactory
   */
  CqAttributesFactory(const CqAttributesPtr& cqAttributes);

  /**
   * Adds a CQ listener to the end of the list of cq listeners on this factory.
   * @param cqListener the CqListener to add to the factory.
   * @throws IllegalArgumentException if <code>cqListener</code> is nullptr
   */
  void addCqListener(const CqListenerPtr& cqListener);

  /**
   * Removes all Cq listeners and then adds each listener in the specified
   * array.
   * @param cqListeners a possibly empty array of listeners to add to this
   * factory.
   * @throws IllegalArgumentException if the <code>cqListeners</code> array has
   * a
   * nullptr element
   */
  void initCqListeners(const std::vector<CqListenerPtr>& cqListeners);

  /**
   * Creates a <code>CqAttributes</code> with the current settings.
   * @return the newly created <code>CqAttributes</code>
   */
  CqAttributesPtr create();

 private:
  CqAttributesPtr m_cqAttributes;
};
}  // namespace client
}  // namespace geode
}  // namespace apache

#endif  // GEODE_CQATTRIBUTESFACTORY_H_
