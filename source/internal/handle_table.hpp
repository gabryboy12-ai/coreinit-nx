#pragma once
#include <switch.h>
#include <unordered_map>

namespace coreinit_nx {

// Mappa l'indirizzo di una struttura allocata dal guest sull'oggetto
// host che ne implementa davvero il comportamento.
//
// Perche' questo approccio invece di scrivere dentro la struttura del
// guest: quella e' in big-endian con il layout Wii U. Usandola solo come
// chiave, l'endianness sparisce dal problema e ci appoggiamo alle
// primitive del kernel Horizon invece di reimplementarle.
//
// Thread-safe: il codice ricompilato e' multi-thread per costruzione.
// Usiamo la Mutex di libnx e non std::mutex per non dipendere dal
// supporto threading della libreria standard sopra newlib.
template <typename Host>
class HandleTable {
public:
    HandleTable() { mutexInit(&m_lock); }

    // Restituisce l'oggetto host associato, creandolo se assente.
    //
    // La creazione pigra e' deliberata: il gioco potrebbe usare un
    // primitivo senza averlo mai inizializzato esplicitamente, se lo
    // inizializzava staticamente. Meglio degradare in modo pulito
    // che crashare.
    Host *get(const void *key) {
        mutexLock(&m_lock);
        Host *host;
        auto it = m_map.find(key);
        if (it != m_map.end()) {
            host = it->second;
        } else {
            host = new Host();
            host->init();
            m_map.emplace(key, host);
        }
        mutexUnlock(&m_lock);
        return host;
    }

    void erase(const void *key) {
        mutexLock(&m_lock);
        auto it = m_map.find(key);
        if (it != m_map.end()) {
            delete it->second;
            m_map.erase(it);
        }
        mutexUnlock(&m_lock);
    }

private:
    ::Mutex m_lock;
    std::unordered_map<const void *, Host *> m_map;
};

} // namespace coreinit_nx
