/*
 * Licensed to the Apache Software Foundation (ASF) under one or more
 * contributor license agreements. See the NOTICE file distributed with
 * this work for additional information regarding copyright ownership.
 * The ASF licenses this file to You under the Apache License, Version 2.0
 * (the "License"); you may not use this file except in compliance with
 * the License. You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */
package org.apache.catalina;

import org.apache.catalina.connector.Connector;
import org.apache.catalina.mapper.Mapper;

public interface Service extends Lifecycle {
    Engine getContainer();
    void setContainer(Engine engine);
    String getName();
    void setName(String name);
    Server getServer();
    void setServer(Server server);
    ClassLoader getParentClassLoader();
    void setParentClassLoader(ClassLoader parent);
    String getDomain();
    void addConnector(Connector connector);
    Connector[] findConnectors();
    void removeConnector(Connector connector);
    void addExecutor(Executor ex);
    Executor[] findExecutors();
    Executor getExecutor(String name);
    void removeExecutor(Executor ex);
    Mapper getMapper();
}
