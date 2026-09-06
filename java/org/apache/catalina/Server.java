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

import java.io.File;
import java.util.concurrent.ScheduledExecutorService;

import org.apache.catalina.deploy.NamingResourcesImpl;
import org.apache.catalina.startup.Catalina;

public interface Server extends Lifecycle {
    NamingResourcesImpl getGlobalNamingResources();
    void setGlobalNamingResources(NamingResourcesImpl globalNamingResources);
    javax.naming.Context getGlobalNamingContext();
    int getPort();
    void setPort(int port);
    int getPortOffset();
    void setPortOffset(int portOffset);
    int getPortWithOffset();
    String getAddress();
    void setAddress(String address);
    String getShutdown();
    void setShutdown(String shutdown);
    ClassLoader getParentClassLoader();
    void setParentClassLoader(ClassLoader parent);
    Catalina getCatalina();
    void setCatalina(Catalina catalina);
    File getCatalinaBase();
    void setCatalinaBase(File catalinaBase);
    File getCatalinaHome();
    void setCatalinaHome(File catalinaHome);
    int getUtilityThreads();
    void setUtilityThreads(int utilityThreads);
    void addService(Service service);
    void await();
    Service findService(String name);
    Service[] findServices();
    void removeService(Service service);
    Object getNamingToken();
    ScheduledExecutorService getUtilityExecutor();
}
