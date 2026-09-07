/*
 *  Licensed to the Apache Software Foundation (ASF) under one or more
 *  contributor license agreements.  See the NOTICE file distributed with
 *  this work for additional information regarding copyright ownership.
 *  The ASF licenses this file to You under the Apache License, Version 2.0
 *  (the "License"); you may not use this file except in compliance with
 *  the License.  You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 *  Unless required by applicable law or agreed to in writing, software
 *  distributed under the License is distributed on an "AS IS" BASIS,
 *  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *  See the License for the specific language governing permissions and
 *  limitations under the License.
 */
package org.apache.tomcat.util.net;

import java.nio.BufferOverflowException;
import java.nio.ByteBuffer;

import org.apache.tomcat.util.buf.ByteBufferUtils;

/**
 * Manages read and write {@link ByteBuffer} instances for a socket connection,
 * handling buffer state transitions between read and write modes.
 */
public class SocketBufferHandler {

    /**
     * A no-op instance with zero-length buffers used when buffering is not required.
     */
    static SocketBufferHandler EMPTY = new SocketBufferHandler(0, 0, false) {
        @Override
        public void expand(int newSize) {
            // NO-OP
        }

        /*
         * Http2AsyncParser$FrameCompletionHandler will return incomplete frame(s) to the buffer. If the previous frame
         * (or concurrent write to a stream) triggered a connection close this call would fail with a
         * BufferOverflowException as data can't be returned to a buffer of zero length. Override the method and make it
         * a NO-OP to avoid triggering the exception.
         */
        @Override
        public void unReadReadBuffer(ByteBuffer returnedData) {
            // NO-OP
        }
    };

    private volatile boolean readBufferConfiguredForWrite = true;
    private volatile ByteBuffer readBuffer;

    private volatile boolean writeBufferConfiguredForWrite = true;
    private volatile ByteBuffer writeBuffer;

    private final boolean direct;

    /**
     * Creates a new SocketBufferHandler with the specified buffer sizes.
     * @param readBufferSize the size of the read buffer in bytes
     * @param writeBufferSize the size of the write buffer in bytes
     * @param direct whether to allocate direct (off-heap) buffers
     */
    public SocketBufferHandler(int readBufferSize, int writeBufferSize, boolean direct) {
        this.direct = direct;
        if (direct) {
            readBuffer = ByteBuffer.allocateDirect(readBufferSize);
            writeBuffer = ByteBuffer.allocateDirect(writeBufferSize);
        } else {
            readBuffer = ByteBuffer.allocate(readBufferSize);
            writeBuffer = ByteBuffer.allocate(writeBufferSize);
        }
    }


    public void configureReadBufferForWrite() {
        setReadBufferConfiguredForWrite(true);
    }

    public void configureReadBufferForRead() {
        setReadBufferConfiguredForWrite(false);
    }

    private void setReadBufferConfiguredForWrite(boolean readBufferConFiguredForWrite) {
        if (this.readBufferConfiguredForWrite != readBufferConFiguredForWrite) {
            if (readBufferConFiguredForWrite) {
                int remaining = readBuffer.remaining();
                if (remaining == 0) {
                    readBuffer.clear();
                } else {
                    readBuffer.compact();
                }
            } else {
                readBuffer.flip();
            }
            this.readBufferConfiguredForWrite = readBufferConFiguredForWrite;
        }
    }

    public ByteBuffer getReadBuffer() {
        return readBuffer;
    }

    public boolean isReadBufferEmpty() {
        if (readBufferConfiguredForWrite) {
            return readBuffer.position() == 0;
        } else {
            return readBuffer.remaining() == 0;
        }
    }

    public void unReadReadBuffer(ByteBuffer returnedData) {
        if (isReadBufferEmpty()) {
            configureReadBufferForWrite();
            readBuffer.put(returnedData);
        } else {
            int bytesReturned = returnedData.remaining();
            if (readBufferConfiguredForWrite) {
                if ((readBuffer.position() + bytesReturned) > readBuffer.capacity()) {
                    throw new BufferOverflowException();
                } else {
                    for (int i = 0; i < readBuffer.position(); i++) {
                        readBuffer.put(i + bytesReturned, readBuffer.get(i));
                    }
                    for (int i = 0; i < bytesReturned; i++) {
                        readBuffer.put(i, returnedData.get());
                    }
                    readBuffer.position(readBuffer.position() + bytesReturned);
                }
            } else {
                int shiftRequired = bytesReturned - readBuffer.position();
                if (shiftRequired > 0) {
                    if ((readBuffer.capacity() - readBuffer.limit()) < shiftRequired) {
                        throw new BufferOverflowException();
                    }
                    int oldLimit = readBuffer.limit();
                    readBuffer.limit(oldLimit + shiftRequired);
                    for (int i = readBuffer.position(); i < oldLimit; i++) {
                        readBuffer.put(i + shiftRequired, readBuffer.get(i));
                    }
                } else {
                    shiftRequired = 0;
                }
                int insertOffset = readBuffer.position() + shiftRequired - bytesReturned;
                for (int i = insertOffset; i < bytesReturned + insertOffset; i++) {
                    readBuffer.put(i, returnedData.get());
                }
                readBuffer.position(insertOffset);
            }
        }
    }

    public void configureWriteBufferForWrite() {
        setWriteBufferConfiguredForWrite(true);
    }

    public void configureWriteBufferForRead() {
        setWriteBufferConfiguredForWrite(false);
    }

    private void setWriteBufferConfiguredForWrite(boolean writeBufferConfiguredForWrite) {
        if (this.writeBufferConfiguredForWrite != writeBufferConfiguredForWrite) {
            if (writeBufferConfiguredForWrite) {
                int remaining = writeBuffer.remaining();
                if (remaining == 0) {
                    writeBuffer.clear();
                } else {
                    writeBuffer.compact();
                    writeBuffer.position(remaining);
                    writeBuffer.limit(writeBuffer.capacity());
                }
            } else {
                writeBuffer.flip();
            }
            this.writeBufferConfiguredForWrite = writeBufferConfiguredForWrite;
        }
    }

    public boolean isWriteBufferWritable() {
        if (writeBufferConfiguredForWrite) {
            return writeBuffer.hasRemaining();
        } else {
            return writeBuffer.remaining() == 0;
        }
    }

    public ByteBuffer getWriteBuffer() {
        return writeBuffer;
    }

    public boolean isWriteBufferEmpty() {
        if (writeBufferConfiguredForWrite) {
            return writeBuffer.position() == 0;
        } else {
            return writeBuffer.remaining() == 0;
        }
    }

    public void reset() {
        readBuffer.clear();
        readBufferConfiguredForWrite = true;
        writeBuffer.clear();
        writeBufferConfiguredForWrite = true;
    }

    public void expand(int newSize) {
        configureReadBufferForWrite();
        readBuffer = ByteBufferUtils.expand(readBuffer, newSize);
        configureWriteBufferForWrite();
        writeBuffer = ByteBufferUtils.expand(writeBuffer, newSize);
    }

    public void free() {
        if (direct) {
            ByteBufferUtils.cleanDirectBuffer(readBuffer);
            ByteBufferUtils.cleanDirectBuffer(writeBuffer);
        }
    }

}
