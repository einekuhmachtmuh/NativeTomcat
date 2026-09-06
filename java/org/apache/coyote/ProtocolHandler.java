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
package org.apache.coyote;

import java.lang.reflect.InvocationTargetException;
import java.util.concurrent.Executor;
import java.util.concurrent.ScheduledExecutorService;

import org.apache.tomcat.util.net.SSLHostConfig;

public interface ProtocolHandler {
    Adapter getAdapter();
    void setAdapter(Adapter adapter);
    Executor getExecutor();
    void setExecutor(Executor executor);
    ScheduledExecutorService getUtilityExecutor();
    void setUtilityExecutor(ScheduledExecutorService utilityExecutor);
    void init() throws Exception;
    void start() throws Exception;
    void pause() throws Exception;
    void resume() throws Exception;
    void stop() throws Exception;
    void destroy() throws Exception;
    void closeServerSocketGraceful();
    long awaitConnectionsClose(long waitMillis);
    boolean isSendfileSupported();
    void addSslHostConfig(SSLHostConfig sslHostConfig);
    void addSslHostConfig(SSLHostConfig sslHostConfig, boolean replace);
    SSLHostConfig[] findSslHostConfigs();
    void addUpgradeProtocol(UpgradeProtocol upgradeProtocol);
    UpgradeProtocol[] findUpgradeProtocols();
    default int getDesiredBufferSize() { return -1; }
    default String getId() { return null; }

    static ProtocolHandler create(String protocol)
            throws ClassNotFoundException, InstantiationException, IllegalAccessException, IllegalArgumentException,
            InvocationTargetException, NoSuchMethodException, SecurityException {
        if (protocol == null || "HTTP/1.1".equals(protocol) ||
                org.apache.coyote.http11.Http11NioProtocol.class.getName().equals(protocol)) {
            return new org.apache.coyote.http11.Http11NioProtocol();
        } else if ("AJP/1.3".equals(protocol) ||
                org.apache.coyote.ajp.AjpNioProtocol.class.getName().equals(protocol)) {
            return new org.apache.coyote.ajp.AjpNioProtocol();
        } else {
            Class<?> clazz = Class.forName(protocol);
            return (ProtocolHandler) clazz.getConstructor().newInstance();
        }
    }
}
