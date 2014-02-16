/**
 * @file handler_impl.hpp
 * @author Robin Dietrich <me (at) invokr (dot) org>
 * @version 1.2
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
 *    Unless required by applicable law or agreed to in writing, software
 *    distributed under the License is distributed on an "AS IS" BASIS,
 *    WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *    See the License for the specific language governing permissions and
 *    limitations under the License.
 *
 */

template <typename Obj, typename Id, typename Data, typename IdSelf>
void handlersub<Obj, Id, Data, IdSelf>::
forward(const id_t& i, Data &&data, uint32_t tick, std::false_type) {
    // check for callback handlers
    auto cbDef = cb.find(i);
    if ((cbDef == cb.end()))
        return;

    // get conversion object
    auto objConv = obj.find(i);
    if (objConv == obj.end())
        BOOST_THROW_EXCEPTION(handlerNoConversionAvailable()
            << (typename EArgT<1, id_t>::info(i))
        );

    // create object
    cbObject<Obj, Id> o(objConv->second(std::move(data)), tick, i);

    // forward to definitive handlers
    for (auto &d : cbDef->second) {
        d(&o);
    }
}

template <typename Obj, typename Id, typename Data, typename IdSelf>
void handlersub<Obj, Id, Data, IdSelf>::
forward(const id_t& i, Data &&data, uint32_t tick, std::true_type) {
    // check for callback handlers
    auto cbDef = cb.find(i);
    if ((cbDef == cb.end()))
        return;

    // create object
    cbObject<Obj, Id> o(std::move(data), tick, i, false);

    // forward to definitive handlers
    for (auto &d : cbDef->second) {
        d(&o);
    }
}

template <typename Obj, typename Id, typename Data, typename IdSelf>
typename handlersub<Obj, Id, Data, IdSelf>::callbackObj_t handlersub<Obj, Id, Data, IdSelf>::
retrieve(const id_t& i, Data &&data, uint32_t tick, std::false_type) {
    // get conversion object
    auto objConv = obj.find(i);
    if (objConv == obj.end())
        BOOST_THROW_EXCEPTION(handlerNoConversionAvailable()
            << (typename EArgT<1, id_t>::info(i))
        );

    // create object
    cbObject<Obj, Id>* o = new cbObject<Obj, Id>(objConv->second(std::move(data)), tick, i);

    return o;
}

template <typename Obj, typename Id, typename Data, typename IdSelf>
typename handlersub<Obj, Id, Data, IdSelf>::callbackObj_t handlersub<Obj, Id, Data, IdSelf>::
retrieve(const id_t& i, Data &&data, uint32_t tick, std::true_type) {
    // create object
    cbObject<Obj, Id>* o = new cbObject<Obj, Id>(std::move(data), tick, i);

    return o;
}

template <typename T1, typename... Rest>
template <typename Type, typename Id>
bool handler<T1, Rest...>::hasCallback(const Id& i, std::true_type) {
    return subhandler.hasCallback(i);
}

template <typename T1, typename... Rest>
template <typename Type, typename Id>
bool handler<T1, Rest...>::hasCallback(const Id& i, std::false_type) {
    return child.template hasCallback<Type, Id>(i);
}

template <typename T1, typename... Rest>
template<typename Type, typename Id, typename Delegate>
void handler<T1, Rest...>::registerCallback(const Id& i, Delegate&& d, std::true_type) {
    subhandler.registerCallback(i, std::move(d));
}

template <typename T1, typename... Rest>
template<typename Type, typename Id, typename Delegate>
void handler<T1, Rest...>::registerCallback(const Id& i, Delegate&& d, std::false_type) {
    child.template registerCallback<Type>(i, std::move(d));
}

template <typename T1, typename... Rest>
template<typename Type, typename Id, typename Delegate>
void handler<T1, Rest...>::removeCallback(const Id& i, Delegate&& d, std::true_type) {
    subhandler.removeCallback(i, std::move(d));
}

template <typename T1, typename... Rest>
template<typename Type, typename Id, typename Delegate>
void handler<T1, Rest...>::removeCallback(const Id& i, Delegate&& d, std::false_type) {
    child.template removeCallback<Type>(i, std::move(d));
}

template <typename T1, typename... Rest>
template<typename Type, typename T, typename Id>
void handler<T1, Rest...>::registerObject(const Id& i, std::true_type) {
    subhandler.template registerObject<T>(i);
}

template <typename T1, typename... Rest>
template<typename Type, typename T, typename Id>
void handler<T1, Rest...>::registerObject(const Id& i, std::false_type) {
    child.template registerObject<Type, T>(i);
}

template <typename T1, typename... Rest>
template <typename Type, typename Id, typename Data>
void handler<T1, Rest...>::forward(Id i, Data data, uint32_t tick, std::true_type) {
    subhandler.forward(std::move(i), std::move(data), std::move(tick));
}

template <typename T1, typename... Rest>
template <typename Type, typename Id, typename Data>
void handler<T1, Rest...>::forward(Id i, Data data, uint32_t tick, std::false_type) {
	child.template forward<Type>(std::move(i), std::move(data), std::move(tick));
}

template <typename T1, typename... Rest>
template <unsigned Type, typename Id, typename Data>
typename handler<T1, Rest...>::template type<Type>::callbackObj_t
handler<T1, Rest...>::retrieve(Id i, Data data, uint32_t tick, std::true_type) {
    return subhandler.retrieve(std::move(i), std::move(data), std::move(tick));
}

template <typename T1, typename... Rest>
template <unsigned Type, typename Id, typename Data>
typename handler<T1, Rest...>::template type<Type>::callbackObj_t
handler<T1, Rest...>::retrieve(Id i, Data data, uint32_t tick, std::false_type) {
    return child.template retrieve<Type-1>(std::move(i), std::move(data), std::move(tick));
}
