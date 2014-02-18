/**
 * @file dem.hpp
 * @author Robin Dietrich <me (at) invokr (dot) org>
 * @version 1.0
 *
 * @par License
 *    Alice Replay Parser
 *    Copyright 2014 Robin Dietrich
 *
 *    Licensed under the Apache License, Version 2.0 (the "License");
 *    you may not use this file except in compliance with the License.
 *    You may obtain a copy of the License at
 *
 *    http://www.apache.org/licenses/LICENSE-2.0
 *
 *    Unless required by applicable law or agreed to in writing, software
 *    distributed under the License is distributed on an "AS IS" BASIS,
 *    WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *    See the License for the specific language governing permissions and
 *    limitations under the License. *
 */

#ifndef _DOTA_DEM_HPP_
#define _DOTA_DEM_HPP_

/// Defines the first 7 bytes to check as the header ID
#define DOTA_DEMHEADERID "PBUFDEM"

#include <alice/exception.hpp>

namespace dota {
    /// Thrown when the replay in inaccesible (e.g. wrong permissions or wrong path)
    CREATE_EXCEPTION( demFileNotAccessible,  "Unable to open file." )
    /// Thrown when the replay has an unlikely small file size
    CREATE_EXCEPTION( demFileTooSmall,       "Filesize is too small." )
    /// Thrown when the header of the file is not matching #DOTA_DEMHEADERID
    CREATE_EXCEPTION( demHeaderMismatch,     "Header ID is not matching." )
    /// Thrown if the end of the file is reached before an operation is completed
    CREATE_EXCEPTION( demUnexpectedEOF,      "Unexpected end of file." )
    /// Thrown when the file is likely corrupted
    CREATE_EXCEPTION( demCorrupted,          "Demo file appears to be corruped." )
    /// Thrown when snappy fails to decompress content
    CREATE_EXCEPTION( demInvalidCompression, "Data uncompression failed." )
    /// Thrown when protobuf fails to parse a message
    CREATE_EXCEPTION( demParsingError,       "Parsing protouf message failed." )
    /// Thrown when the size of a single messages would cause a buffer overflow
    CREATE_EXCEPTION( demMessageToBig,       "Messagesize is to big." )

    /** DEM file header, used for verification purposes */
    struct demHeader_t {
        char headerid[ 8 ]; // needs to equal DOTA_HEADERID
        int32_t offset;
    };

    /** DEM message read from the file */
    struct demMessage_t {
        /** Was this message compressed? */
        bool compressed;
        /** Game Tick */
        uint32_t tick;
        /** Message type */
        uint32_t type;
        /** Pointing to the start of the message */
        const char* msg;
        /** Size of the message */
        std::size_t size;
    };
}

#endif // _DOTA_DEM_HPP_