/*
 * Licensed to the Apache Software Foundation (ASF) under one or more
 * contributor license agreements.  See the NOTICE file distributed with
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
package org.apache.juli.logging;

/**
 * A simple logging interface abstracting logging APIs. In order to be instantiated successfully by {@link LogFactory},
 * classes that implement this interface must have a constructor that takes a single String parameter representing the
 * "name" of this Log.
 * <p>
 * The six logging levels used by <code>Log</code> are (in order):
 * <ol>
 * <li>trace (the least serious)</li>
 * <li>debug</li>
 * <li>info</li>
 * <li>warn</li>
 * <li>error</li>
 * <li>fatal (the most serious)</li>
 * </ol>
 * <p>
 * The mapping of these log levels to the concepts used by the underlying logging system is implementation dependent.
 * The implementation should ensure, though, that this ordering behaves as expected.
 * <p>
 * Performance is often a logging concern. By examining the appropriate property, a component can avoid expensive
 * operations (producing information to be logged).
 * <p>
 * For example, <code>
 *    if (log.isDebugEnabled()) {
 *        ... do something expensive ...
 *        log.debug(theResult);
 *    }
 * </code>
 * <p>
 * Configuration of the underlying logging system will generally be done external to the Logging APIs, through whatever
 * mechanism is supported by that system.
 */
public interface Log {

    boolean isDebugEnabled();
    boolean isErrorEnabled();
    boolean isFatalEnabled();
    boolean isInfoEnabled();
    boolean isTraceEnabled();
    boolean isWarnEnabled();

    void trace(Object message);
    void trace(Object message, Throwable t);
    void debug(Object message);
    void debug(Object message, Throwable t);
    void info(Object message);
    void info(Object message, Throwable t);
    void warn(Object message);
    void warn(Object message, Throwable t);
    void error(Object message);
    void error(Object message, Throwable t);
    void fatal(Object message);
    void fatal(Object message, Throwable t);
}
