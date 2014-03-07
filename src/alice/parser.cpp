/**
 * @file parser.cpp
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
 *    limitations under the License.
 */

#include <alice/demo.pb.h>
#include <alice/netmessages.pb.h>
#include <alice/usermessages.pb.h>
#include <alice/dota_usermessages.pb.h>

#include <alice/bitstream.hpp>
#include <alice/parser.hpp>

namespace dota {
    parser::parser(const settings s, dem_stream *stream) : set(s), stream(stream), tick(0), sendtableId(-1), stringtableId(-1) {
        handlerRegisterCallback((&handler), msgDem, DEM_Packet,       parser, handlePacket)
        handlerRegisterCallback((&handler), msgDem, DEM_SignonPacket, parser, handlePacket)
        handlerRegisterCallback((&handler), msgNet, svc_UserMessage,  parser, handleUserMessage)

        if (set.parse_entities) {
            handlerRegisterCallback((&handler),  msgDem, DEM_ClassInfo,  parser, handleClasses)
            handlerRegisterCallback((&handler),  msgDem, DEM_SendTables, parser, handleSendTables)

            handlerRegisterCallback((&handler), msgNet, svc_ServerInfo, parser, handleServerInfo)
            handlerRegisterCallback((&handler), msgNet, svc_SendTable,  parser, handleSendTable)
        }

        if (set.parse_stringtables) {
            handlerRegisterCallback((&handler), msgNet, svc_CreateStringTable, parser, handleCreateStringtable)
            handlerRegisterCallback((&handler), msgNet, svc_UpdateStringTable, parser, handleUpdateStringtable)
        }

        if (set.parse_entities) {
            // allocate memory for entities
            entities.resize(DOTA_MAX_ENTITIES);

            // callback handlers
            handlerRegisterCallback((&handler), msgNet, svc_PacketEntities, parser, handleEntity)
        }

        // Get unique / non-unique IDs for all messages
        registerTypes();
    }

    parser::~parser() {
        delete stream;
    }

    void parser::open(std::string path) {
        file = path;
        stream->open(path);

        // let handlers know we begin to parse
        handler.forward<msgStatus>(REPLAY_START, REPLAY_START, 0);
    }

    bool parser::good() {
        return stream->good();
    }

    void parser::read() {
        // Read a single message
        //
        // If forward_dem is set, packages not handled by the parser are skipped because no
        // one would get them anyway.
        // This increases the read performance by about 20% for the DEM part.
        demMessage_t msg = stream->read(!set.forward_dem);

        // update current tick
        tick = msg.tick;

        // Forward messages via the handler or handle them internaly
        if (set.forward_dem) {
            handler.forward<msgDem>(msg.type, std::move(msg), msg.tick);
        } else {
            #ifndef _MSC_VER
            switch (msg.type) {
                case DEM_ClassInfo: {
                    if (set.parse_entities) {
                        auto cb = handler.retrieve<msgDem::id>(msg.type, std::move(msg), msg.tick);
                        handleClasses(&cb);
                        cb.free();
                    }
                } break;
                case DEM_SignonPacket:
                case DEM_Packet: {
                    auto cb = handler.retrieve<msgDem::id>(msg.type, std::move(msg), msg.tick);
                    handlePacket(&cb);
                    cb.free();
                } break;
                case DEM_SendTables: {
                    if (set.parse_entities) {
                        auto cb = handler.retrieve<msgDem::id>(msg.type, std::move(msg), msg.tick);
                        handleSendTables(&cb);
                        cb.free();
                    }
                } break;
            }
            #else // _MSC_VER
                handler.forward<msgDem>(msg.type, std::move(msg), msg.tick);
            #endif // _MSC_VER
        }
    }

    void parser::handle() {
        // parse all messages while there is data available
        while (stream->good()) {
            read();
        }

        // let handlers know we are done
        handler.forward<msgStatus>(REPLAY_FINISH, REPLAY_FINISH, tick);
    }

    void parser::skipTo(uint32_t second) {
        uint32_t min = second/60;
        int32_t sec = second%60;

        // make sure we have a valid state before skipping ahead / back
        while (tick < 30) {
            read();
        }

        // clear all entities
        entities.clear();
        entities.resize(DOTA_MAX_ENTITIES);

        // skip to the fullpacket
        stream->move(min);

        // get the next full package in the stram
        demMessage_t msg;
        do {
            msg = stream->read(false);
        } while (msg.type != 13 && good());

        // parse it
        auto full = handler.retrieve<msgDem::id>(msg.type, std::move(msg), msg.tick);
        CDemoFullPacket* p = full.get<CDemoFullPacket>();

        for (auto &tbl : p->string_table().tables()) {
            auto it = stringtables.findKey(tbl.table_name());
            if (it == stringtables.end())
                continue;

            for (auto &item : tbl.items()) {
                it->value.set(item.str(), item.data());
            }

            for (auto &item : tbl.items_clientside()) {
                it->value.set(item.str(), item.data());
            }
        }

        // forward packets
        const std::string &data = p->packet().data();
        forwardMessageContainer<msgNet>(data.c_str(), data.size(), msg.tick);

        // asume 1 tick 2 / secs
        for (; sec > 0; sec -= 2) {
            read();
        }
    }

    handler_t* parser::getHandler() {
        return &handler;
    }

    const flatsendtable& parser::getFlattable(const std::string &tbl) {
        auto it = flattables.find(tbl);
        if (it == flattables.end())
            BOOST_THROW_EXCEPTION( sendtableUnkownTable()
                << EArg<1>::info(tbl)
            );

        return it->second;
    }

    uint32_t parser::getEntityIdFor(std::string name) {
        for (auto &e : clist) {
            if (e.second.networkName == name)
                return e.second.id;
        }

        BOOST_THROW_EXCEPTION( aliceInvalidDefinition()
            << EArg<1>::info(name)
        );

        return 0;
    }

    typename parser::entityMap& parser::getEntities() {
        return entities;
    }

    typename parser::stringtableMap& parser::getStringtables() {
        return stringtables;
    }

    typename parser::flatMap& parser::getFlattables() {
        return flattables;
    }

    typename parser::sendtableMap& parser::getSendtables() {
        return sendtables;
    }

    std::vector<uint32_t> parser::findEntityIdFor(std::string name) {
        std::vector<uint32_t> ret;
        uint32_t length = name.size();

        for (auto &e : clist) {
            if (e.second.networkName.substr(0, length) == name)
                ret.push_back(e.second.id);
        }

        return ret;
    }

    uint32_t parser::readVarInt(const char* data, uint32_t& count) {
        count  = 0;
        uint32_t result = 0;
        char buffer;

        do {
            if (count == 5) {
                BOOST_THROW_EXCEPTION(demCorrupted()
                    << EArg<1>::info(file)
                );
            } else if (!stream->good()) {
                BOOST_THROW_EXCEPTION(demUnexpectedEOF()
                    << EArg<1>::info(file)
                );
            } else {
                // append to result
                buffer = data[0];
                result |= (uint32_t)(buffer & 0x7F) << ( 7 * count );

                // make sure we don't read the same data twice
                data = data+1;

                // increase count, as we read 8 bits this should never iterate more than 4 times
                ++count;
            }
        } while (buffer & 0x80);

        return result;
    }

    void parser::handleClasses(handlerCbType(msgDem) msg) {
        CDemoClassInfo* m = msg->get<CDemoClassInfo>();

        for (int32_t i = 0; i < m->classes_size(); ++i) {
            const CDemoClassInfo::class_t &c = m->classes(i);
            clist.set(c.class_id(), entity_description{c.class_id(), c.table_name(), c.network_name()});
        }

        flattenSendtables();
        handler.forward<msgStatus>(REPLAY_FLATTABLES, REPLAY_FLATTABLES, msg->tick);
    }

    void parser::handlePacket(handlerCbType(msgDem) msg) {
        CDemoPacket* m = msg->get<CDemoPacket>();
        const std::string &data = m->data();

        // forward as Net Message
        forwardMessageContainer<msgNet>(data.c_str(), data.size(), msg->tick);
    }

    void parser::handleSendTables(handlerCbType(msgDem) msg) {
        CDemoSendTables* m = msg->get<CDemoSendTables>();
        const std::string &data = m->data();

        // forward as Net Message
        forwardMessageContainer<msgNet>(data.c_str(), data.size(), msg->tick);
    }

    void parser::handleUserMessage(handlerCbType(msgDem) msg) {
        CSVCMsg_UserMessage* m = msg->get<CSVCMsg_UserMessage>();
        const std::string &data = m->msg_data();
        uint32_t type = static_cast<uint32_t>(m->msg_type());

        // forward as user message
        handler.forward<msgUser>(type, demMessage_t{0, msg->tick, type, data.c_str(), data.size()}, msg->tick);
    }

    void parser::handleServerInfo(handlerCbType(msgNet) msg) {
        CSVCMsg_ServerInfo* m = msg->get<CSVCMsg_ServerInfo>();
        clist.reserve(m->max_classes());
        classBits = std::ceil(log2(m->max_classes()));
    }

    void parser::handleSendTable(handlerCbType(msgNet) msg) {
        int32_t stableId = ++sendtableId;
        CSVCMsg_SendTable* m = msg->get<CSVCMsg_SendTable>();

        sendtable tbl(m->net_table_name(), m->needs_decoder());
        for (int32_t i = 0; i < m->props_size(); ++i) {
            tbl.insert(new sendprop(m->props(i), m->net_table_name()));
        }

        sendtables.insert(sendtableMap::entry_type{m->net_table_name(), ++stableId, std::move(tbl)});
    }

    void parser::handleCreateStringtable(handlerCbType(msgNet) msg) {
        CSVCMsg_CreateStringTable* m = msg->get<CSVCMsg_CreateStringTable>();

        // global unique id for ordered indexes
        int32_t tableid = ++stringtableId;

        if (m->user_data_size_bits() & 2)
            return;

        if (set.skip_stringtables.count(m->name()))
            return;

        // add table to table list
        stringtables.insert(stringtableMap::entry_type{m->name(), tableid, stringtable(m)});
    }

    void parser::handleUpdateStringtable(handlerCbType(msgNet) msg) {
        CSVCMsg_UpdateStringTable* m = msg->get<CSVCMsg_UpdateStringTable>();

        // only update tables which we handle
        auto it = stringtables.findIndex(m->table_id());
        if (it == stringtables.endIndex())
            return;

        it->value.update(m);
    }

    bool parser::isSkipped(entity &e) {
        // get classid
        uint32_t eId = e.getClassId();

        // check if the entity is skipped because there is no handler
        bool skipU = (set.skip_unsubscribed_entities && !handler.hasCallback<msgEntity>(eId));
        if (skipU) return true;

        // check to skip if entity is in the ignore set
        bool skipE = set.skip_entities.empty() ? false : set.skip_entities.count(eId);
        if (skipE) return true;

        return false;
    }

    void parser::handleEntity(handlerCbType(msgNet) msg) {
        CSVCMsg_PacketEntities* e = msg->get<CSVCMsg_PacketEntities>(); // get entity
        bitstream stream(e->entity_data()); // bistream from the entity data

        uint32_t eId = -1;          // Entity ID
        entity::state_type eType;   // Entity update Type (create, update, delete)

        // baseline table containing default entity values
        stringtableMap::key_iterator it = stringtables.findKey(BASELINETABLE);
        if (it == stringtables.end())
            BOOST_THROW_EXCEPTION( aliceBaselineNotFound() );

        const stringtable &baseline = it->value;

        for (int32_t i = 0; i < e->updated_entries(); ++i) {
            // read update type and id from header
            entity::readHeader(eId, stream, eType);

            switch(eType) {
                // entity is being created
                case entity::state_created: {
                    if (eId > DOTA_MAX_ENTITIES)
                        BOOST_THROW_EXCEPTION( entityIdToLarge()
                            << (EArgT<1, uint32_t>::info(eId))
                        );

                    uint32_t classId = stream.read(classBits); // <- points to id in the entity list
                    // serial was never used to lets just always skip it
                    // uint32_t serial = stream.read(10);
                    stream.seekForward(10);

                    const entity_list::value_type &eClass = clist.get(classId);
                    const flatsendtable &f = getFlattable(eClass.name);

                    entity &ent = entities[eId];
                    if (!ent.isInitialized()) {
                        // create the entity
                        entities[eId] = entity(eId, eClass, f);
                        ent = entities[eId];
                    } else {
                        // entity already exists, update it as overwritten
                        ent.update(eId, eClass, f);
                        ent.setState(entity::state_overwritten);
                    }

                    if (isSkipped(ent)) {
                        ent.skip(stream);
                    } else {
                        // read updates from baseline and current data
                        bitstream baselineStream(baseline.get(std::to_string(classId)));
                        ent.updateFromBitstream(baselineStream);
                        ent.updateFromBitstream(stream);

                        // forward to handler
                        handler.forward<msgEntity>(ent.getClassId(), &ent, 0);
                    }
                } break;
                // entity is being updated
                case entity::state_updated: {
                    if (eId > DOTA_MAX_ENTITIES)
                        BOOST_THROW_EXCEPTION( entityIdToLarge()
                            << (EArgT<1, uint32_t>::info(eId))
                        );

                    entity& ent = entities[eId];
                    if (ent.isInitialized()) {
                        if (isSkipped(ent)) {
                            ent.skip(stream);
                        } else {
                            ent.updateFromBitstream(stream);
                            ent.setState(entity::state_updated);
                            handler.forward<msgEntity>(ent.getClassId(), &ent, 0);
                        }
                    } else {
                        BOOST_THROW_EXCEPTION( aliceInvalidId()
                            << (EArgT<1, uint32_t>::info(eId))
                        );
                    }
                } break;
                // entity is being deleted
                case entity::state_deleted: {
                    if (eId > DOTA_MAX_ENTITIES)
                        BOOST_THROW_EXCEPTION( entityIdToLarge()
                            << (EArgT<1, uint32_t>::info(eId))
                        );

                    entity& ent = entities[eId];
                    if (ent.isInitialized()) {
                        if (!isSkipped(ent)) {
                            ent.setState(entity::state_deleted);
                            handler.forward<msgEntity>(ent.getClassId(), &ent, 0);
                        }

                        entities[eId] = entity();
                    } else {
                        BOOST_THROW_EXCEPTION( aliceInvalidId()
                            << (EArgT<1, uint32_t>::info(eId))
                        );
                    }
                } break;
                default:
                    // ignore
                    break;
            }
        }

        // all entities in list are marked as removed
        if (e->is_delta()) {
            while (stream.read(1)) {
                eId = stream.read(11);

                entity& ent = entities[eId];
                if (ent.isInitialized()) {
                    if (!isSkipped(ent)) {
                        ent.setState(entity::state_deleted);
                        handler.forward<msgEntity>(ent.getClassId(), &ent, 0);
                    }

                    entities[eId] = entity();
                }
            }
        }
    }

    void parser::flattenSendtables() {
        // Tieing dependend properties
        for (auto it = sendtables.beginIndex(); it != sendtables.endIndex(); ++it) {
            sendprop* last = nullptr;
            for (auto &prop : it->value) {
                if (prop.value->getType() == sendprop::T_Array) {
                    if (last)
                        prop.value->setArrayType(last);
                    else
                        BOOST_THROW_EXCEPTION( aliceInvalidArrayProp() );
                }
                last = prop.value;
            }
        }

        // Building flattables
        for (auto it = sendtables.beginIndex(); it != sendtables.endIndex(); ++it) {
            auto table = *it;

            std::set<std::string> excludes;  // names of excluded properties
            std::vector<sendprop*> props;    // list of property classes
            std::vector<std::string> names;   // list of property names when traversing

            // Building excludes
            buildExcludeList(table.value, excludes);

            // Building hierarchy
            buildHierarchy(table.value, excludes, props, table.value.getName(), names);

            // Sorting tables
            std::set<uint32_t> priorities({64}); // list of all possible priorities
            for (auto &it : props) {
                priorities.insert(it->getPriority());
            }

            std::size_t offset = 0;
            for (auto &prio : priorities) {
                std::size_t cursor = offset;

                while (cursor < props.size()) {
                    auto prop = props[cursor];

                    if ((prop->getPriority() == prio) || ((SPROP_CHANGES_OFTEN & prop->getFlags()) && (prio == 64))) {
                        std::swap(props[cursor], props[offset]);
                        ++offset;
                    }
                    ++cursor;
                }
            }

            // insert stuff into flat table
            assert(props.size() == names.size());
            flattables.emplace(table.key, flatsendtable{table.key, std::move(props), std::move(names)});
        }
    }

    void parser::buildExcludeList(const sendtable &tbl, std::set<std::string> &excludes) {
        for (auto &p : tbl) {
            sendprop* pr = p.value;

            if (SPROP_EXCLUDE & pr->getFlags()) {
                // adding an exclude
                excludes.insert(std::string(pr->getClassname() + pr->getName()));
            } else if (pr->getType() == sendprop::T_DataTable) { // sub table can also point to excluded data
                sendtableMap::key_iterator it = sendtables.findKey(pr->getClassname());
                if (it == sendtables.end())
                    BOOST_THROW_EXCEPTION( sendtableUnkownTable()
                        << EArg<1>::info(pr->getClassname())
                    );

                buildExcludeList(it->value, excludes);
            }
        }
    }

    void parser::buildHierarchy(const sendtable &tbl, std::set<std::string> &excludes, std::vector<sendprop*> &props, std::string base, std::vector<std::string>& ids) {
        // Building hierarchy for the table
        std::vector<sendprop*> p;
        gatherProperties(tbl, p, excludes, props, base, ids);

        for (auto ExcludedProps : p) {
            props.push_back(ExcludedProps);
        }
    }

    void parser::gatherProperties(const sendtable &tbl, std::vector<sendprop*> &dt_prop, std::set<std::string> &excludes, std::vector<sendprop*> &props, std::string base, std::vector<std::string>& ids) {
        for (auto &p : tbl) {
            sendprop* pr = p.value;

            // skip excluded properties
            if ((SPROP_EXCLUDE | SPROP_INSIDEARRAY) & pr->getFlags()) {
                continue;
            } else if (excludes.count(std::string(tbl.getName() + pr->getName()))) {
                continue;
            }

            // check for new data
            if (pr->getType() == sendprop::T_DataTable) {
                auto it = sendtables.findKey(pr->getClassname());
                if (it == sendtables.end())
                    BOOST_THROW_EXCEPTION( sendtableUnkownTable()
                        << EArg<1>::info(pr->getClassname())
                    );
                const sendtable &dtTbl = it->value;

                if (SPROP_COLLAPSIBLE & pr->getFlags()) {
                    gatherProperties(dtTbl, dt_prop, excludes, props, base, ids);
                } else {
                    buildHierarchy(dtTbl, excludes, props, base+"."+pr->getName(), ids);
                }
            } else {
                ids.push_back(base+"."+pr->getName());
                dt_prop.push_back(p.value);
            }
        }
    }

    void parser::registerTypes() {
        #define regDem( _type ) handlerRegisterObject((&handler), msgDem, DEM_ ## _type, CDemo ## _type)
        #define regNet( _type ) handlerRegisterObject((&handler), msgNet, net_ ## _type, CNETMsg_ ## _type)
        #define regSvc( _type ) handlerRegisterObject((&handler), msgNet, svc_ ## _type, CSVCMsg_ ## _type)
        #define regUsr( _type ) handlerRegisterObject((&handler), msgUser, UM_ ## _type, CUserMsg_ ## _type)
        #define regUsrDota( _type ) handlerRegisterObject((&handler), msgUser, DOTA_UM_ ## _type, CDOTAUserMsg_ ## _type)

        regDem( FileHeader )                                         // 1
        regDem( FileInfo )                                           // 2
        regDem( SyncTick )                                           // 3
        regDem( SendTables )                                         // 4
        regDem( ClassInfo )                                          // 5
        regDem( StringTables )                                       // 6
        regDem( Packet )                                             // 7
        handlerRegisterObject((&handler), msgDem, DEM_SignonPacket, CDemoPacket) // 8
        regDem( ConsoleCmd )                                         // 9
        regDem( CustomData )                                         // 10
        regDem( CustomDataCallbacks )                                // 11
        regDem( UserCmd )                                            // 12
        regDem( FullPacket )                                         // 13

        regNet( NOP )               // 0
        regNet( Disconnect )        // 1
        regNet( File )              // 2
        regNet( SplitScreenUser )   // 3
        regNet( Tick )              // 4
        regNet( StringCmd )         // 5
        regNet( SetConVar )         // 6
        regNet( SignonState )       // 7
        regSvc( ServerInfo )        // 8
        regSvc( SendTable )         // 9
        regSvc( ClassInfo )         // 10
        regSvc( SetPause )          // 11
        regSvc( CreateStringTable ) // 12
        regSvc( UpdateStringTable ) // 13
        regSvc( VoiceInit )         // 14
        regSvc( VoiceData )         // 15
        regSvc( Print )             // 16
        regSvc( Sounds )            // 17
        regSvc( SetView )           // 18
        regSvc( FixAngle )          // 19
        regSvc( CrosshairAngle )    // 20
        regSvc( BSPDecal )          // 21
        regSvc( SplitScreen )       // 22
        regSvc( UserMessage )       // 23
        regSvc( GameEvent )         // 25
        regSvc( PacketEntities )    // 26
        regSvc( TempEntities )      // 27
        regSvc( Prefetch )          // 28
        regSvc( Menu )              // 29
        regSvc( GameEventList )     // 30
        regSvc( GetCvarValue )      // 31
        regSvc( PacketReliable )    // 32

        regUsr( AchievementEvent )  // 1
        regUsr( CloseCaption )      // 2
        regUsr( CurrentTimescale )  // 4
        regUsr( DesiredTimescale )  // 5
        regUsr( Fade )              // 6
        regUsr( GameTitle )         // 7
        regUsr( Geiger )            // 8
        regUsr( HintText )          // 9
        regUsr( HudMsg )            // 10
        regUsr( HudText )           // 11
        regUsr( KeyHintText )       // 12
        regUsr( MessageText )       // 13
        regUsr( RequestState )      // 14
        regUsr( ResetHUD )          // 15
        regUsr( Rumble )            // 16
        regUsr( SayText )           // 17
        regUsr( SayText2 )          // 18
        regUsr( SayTextChannel )    // 19
        regUsr( Shake )             // 20
        regUsr( ShakeDir )          // 21
        regUsr( StatsCrawlMsg )     // 22
        regUsr( StatsSkipState )    // 23
        regUsr( TextMsg )           // 24
        regUsr( Tilt )              // 25
        regUsr( Train )             // 26
        regUsr( VGUIMenu )          // 27
        regUsr( VoiceMask )         // 28
        regUsr( VoiceSubtitle )     // 29
        regUsr( SendAudio )         // 30

        //regUsrDota( AddUnitToSelection )      // 64
        regUsrDota( AIDebugLine )               // 65
        regUsrDota( ChatEvent )                 // 66
        regUsrDota( CombatHeroPositions )       // 67
        regUsrDota( CombatLogData )             // 68
        regUsrDota( CombatLogShowDeath )        // 70
        regUsrDota( CreateLinearProjectile )    // 71
        regUsrDota( DestroyLinearProjectile )   // 72
        regUsrDota( DodgeTrackingProjectiles )  // 73
        regUsrDota( GlobalLightColor )          // 74
        regUsrDota( GlobalLightDirection )      // 75
        regUsrDota( InvalidCommand )            // 76
        regUsrDota( LocationPing )              // 77
        regUsrDota( MapLine )                   // 78
        regUsrDota( MiniKillCamInfo )           // 79
        regUsrDota( MinimapDebugPoint )         // 80
        regUsrDota( MinimapEvent )              // 81
        regUsrDota( NevermoreRequiem )          // 82
        regUsrDota( OverheadEvent )             // 83
        regUsrDota( SetNextAutobuyItem )        // 84
        regUsrDota( SharedCooldown )            // 85
        regUsrDota( SpectatorPlayerClick )      // 86
        regUsrDota( TutorialTipInfo )           // 87
        regUsrDota( UnitEvent )                 // 88
        regUsrDota( ParticleManager )           // 89
        regUsrDota( BotChat )                   // 90
        regUsrDota( HudError )                  // 91
        regUsrDota( ItemPurchased )             // 92
        regUsrDota( Ping )                      // 93
        regUsrDota( ItemFound )                 // 94
        //regUsrDota( CharacterSpeakConcept )   // 95
        regUsrDota( SwapVerify )                // 96
        regUsrDota( WorldLine )                 // 97
        regUsrDota( TournamentDrop )            // 98
        regUsrDota( ItemAlert )                 // 99
        regUsrDota( HalloweenDrops )            // 100
        regUsrDota( ChatWheel )                 // 101
        regUsrDota( ReceivedXmasGift )          // 102
        regUsrDota( UpdateSharedContent )       // 103
        regUsrDota( TutorialRequestExp )        // 104
        regUsrDota( TutorialPingMinimap )       // 105
        handlerRegisterObject((&handler), msgUser, DOTA_UM_GamerulesStateChanged, CDOTA_UM_GamerulesStateChanged) // 106
        regUsrDota( ShowSurvey )                // 107
        regUsrDota( TutorialFade )              // 108
        regUsrDota( AddQuestLogEntry )          // 109
        regUsrDota( SendStatPopup )             // 110
        regUsrDota( TutorialFinish )            // 111
        regUsrDota( SendRoshanPopup )           // 112
        regUsrDota( SendGenericToolTip )        // 113
        regUsrDota( SendFinalGold )             // 114

        #undef regDem
        #undef regNet
        #undef regSvc
        #undef regUsr
        #undef regUsrDota
    }
}