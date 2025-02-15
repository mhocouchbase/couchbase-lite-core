//
// Listener.hh
//
// Copyright 2017-Present Couchbase, Inc.
//
// Use of this software is governed by the Business Source License included
// in the file licenses/BSL-Couchbase.txt.  As of the Change Date specified
// in that file, in accordance with the Business Source License, use of this
// software will be governed by the Apache License, Version 2.0, included in
// the file licenses/APL2.txt.
//

#pragma once
#include "fleece/RefCounted.hh"
#include "fleece/InstanceCounted.hh"
#include "c4Listener.hh"
#include "FilePath.hh"
#include <map>
#include <mutex>
#include <optional>
#include <vector>

namespace litecore { namespace REST {

    /** Abstract superclass of network listeners that can serve access to databases.
        Subclassed by RESTListener. */
    class Listener : public fleece::RefCounted, public fleece::InstanceCountedIn<Listener> {
    public:
        using Config = C4ListenerConfig;

        static constexpr uint16_t kDefaultPort = 4984;

        Listener(const Config &config);
        virtual ~Listener() =default;

        /** Determines whether a database name is valid for use as a URI path component.
            It must be nonempty, no more than 240 bytes long, not start with an underscore,
            and contain no control characters. */
        static bool isValidDatabaseName(const std::string&);

        /** Given a filesystem path to a database, returns the database name.
            (This takes the last path component and removes the ".cblite2" extension.)
            Returns an empty string if the path is not a database, or if the name would not
            be valid according to isValidDatabaseName(). */
        static std::string databaseNameFromPath(const FilePath&);

        /** Makes a database visible via the REST API.
            Retains the C4Database; the caller does not need to keep a reference to it. */
        bool registerDatabase(C4Database* NONNULL, std::optional<std::string> name =std::nullopt);

        /** Unregisters a database by name.
            The C4Database will be closed if there are no other references to it. */
        bool unregisterDatabase(std::string name);

        bool unregisterDatabase(C4Database *db);

        /** Returns the database registered under the given name. */
        fleece::Retained<C4Database> databaseNamed(const std::string &name) const;

        /** Returns the name a database is registered under. */
        std::optional<std::string> nameOfDatabase(C4Database* NONNULL) const;

        /** Returns all registered database names. */
        std::vector<std::string> databaseNames() const;

        /** Returns the number of client connections. */
        virtual int connectionCount() =0;

        /** Returns the number of active client connections (for some definition of "active"). */
        virtual int activeConnectionCount() =0;

    protected:
        mutable std::mutex _mutex;
        Config _config;
        std::map<std::string, fleece::Retained<C4Database>> _databases;
    };

} }
