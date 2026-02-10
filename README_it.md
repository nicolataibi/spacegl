# Space GL: 3D Multi-User Client-Server Edition
## Un gioco di esplorazione e combattimento spaziale
## "Per Tenebras, Lumen" ("Attraverso le tenebre, la luce")
### Website: https://github.com/nicolataibi/spacegl
### Authors: Nicola Taibi, Supported by Google Gemini
### Copyright (C) 2026 Nicola Taibi - Licensed under GPL-3.0-or-later
**Persistent Galaxy Tactical Navigation & Combat Simulator**

<table>
  <tr>
    <td><img src="readme_assets/startup.jpg" alt="Space GL Startup" width="400"/></td>
    <td><img src="readme_assets/running.jpg" alt="Space GL Running" width="400"/></td>
  </tr>
  <tr>
    <td><img src="readme_assets/combat.jpg" alt="Space Combat" width="400"/></td>
    <td><img src="readme_assets/comets.jpg" alt="Comets and Asteroids" width="400"/></td>
  </tr>
  <tr>
    <td><img src="readme_assets/space-monster.jpg" alt="Space Monster" width="400"/></td>
    <td><img src="readme_assets/rift.jpg" alt="Spatial Rift" width="400"/></td>
  </tr>
  <tr>
    <td><img src="readme_assets/shields.jpg" alt="Defensive Shields" width="400"/></td>
    <td></td>
  </tr>
  <tr>
    <td><img src="readme_assets/wormhole-enter.jpg" alt="Wormhole Entry" width="400"/></td>
    <td><img src="readme_assets/wormhole-exit.jpg" alt="Wormhole Exit" width="400"/></td>
  </tr>
</table>

Space GL è un simulatore spaziale avanzato che unisce la profondità strategica dei classici giochi testuali anni '70 con un'architettura moderna Client-Server e una visualizzazione 3D accelerata hardware.

---

## 🚀 Guida Rapida all'Avvio (Quick Start)

### 1. Installazione Dipendenze (Linux)
```bash
# Ubuntu / Debian
sudo apt-get install build-essential freeglut3-dev libglu1-mesa-dev libglew-dev libssl-dev

# Fedora / Red Hat
sudo dnf groupinstall "Development Tools"
sudo dnf install freeglut-devel mesa-libGLU-devel glew-devel openssl-devel
```

### 2. Compilazione
Compila il progetto per generare gli eseguibili aggiornati:
```bash
make
```

### 3. Guida Operativa ai Comandi e Crittografia Dello spazio profondo

Il ponte di comando di Space GL opera tramite un'interfaccia a riga di comando (CLI) ad alta precisione. Oltre ai comandi di navigazione e combattimento, il simulatore implementa un sofisticato sistema di **Guerra Elettronica** basato su crittografia reale.

#### 🛰️ Comandi di Navigazione Avanzata e Utility
*   `nav <H> <M> <W> [F]`: **Hyperdrive Navigation**. Rotta di Iperspazio verso coordinate relative. `H`: Heading (0-359), `M`: Mark (-90/+90), `W`: Distanza in quadranti, `F`: Fattore Hyperdrive opzionale (1.0 - 9.9).
*   `imp <H> <M> <S>`: **Impulse Drive**. Navigazione sub-luce nel settore attuale. `S`: Velocità in percentuale (1-100%). Usare `imp <S>` per cambiare solo velocità.
*   `jum <Q1> <Q2> <Q3>`: **Wormhole Jump**. Genera un tunnel spaziale verso un quadrante lontano. Richiede **5000 energia e 1 Cristallo di Aetherium**.
*   `apr <ID> [DIST]`: **Automatic Approach**. Il pilota automatico intercetta l'oggetto specificato alla distanza desiderata (default 2.0). Funziona in tutta la galassia per navi e comete.
*   `cha`: **Chase Target**. Insegue attivamente il bersaglio attualmente agganciato (`lock`).
*   `rep <ID>`: **Repair System**. Avvia le riparazioni su un sottosistema (1: Hyperdrive, 2: Impulso, 3: Sensori, 4: Raggio Ionico, 5: Siluri, ecc.).
*   `inv`: **Inventory Report**. Elenco dettagliato delle risorse in stiva (Dilithio, Neo-Titanium, Gas, ecc.).
*   `dam`: **Damage Report**. Stato dettagliato dell'integrità dello scafo e dei sistemi.
*   `cal <Q1> <Q2> <Q3>`: **Hyperdrive Calculator**. Calcola il vettore verso il centro di un quadrante distante.
*   `cal <Q1> <Q2> <Q3> <X> <Y> <Z>`: **Pinpoint Calculator**. Calcola il vettore verso coordinate di settore precise `[X, Y, Z]` in un quadrante distante. Fornisce tempi di arrivo e suggerisce il comando `nav` esatto da copiare.
*   `ical <X> <Y> <Z>`: **Impulse Calculator**. Fornisce la soluzione di navigazione completa per coordinate di settore precise [0.0 - 10.0], incluso il tempo di percorrenza in base all'attuale potenza dei motori.
*   `who`: **Captains Registry**. Elenca tutti i comandanti attualmente attivi nella galassia, i loro ID di tracciamento e la posizione attuale. Fondamentale per identificare alleati o potenziali predatori prima di entrare in un settore.
*   `sta`: **Status Report**. Diagnostica completa dei sistemi, inclusi i livelli di energia, integrità hardware e ripartizione della potenza.
*   `hull`: **Composite Reinforcement**. Se possiedi **100 unità di Composite** in stiva, questo comando applica una placcatura rinforzata allo scafo (+500 HP di scudo fisico), visibile in oro nell'HUD.

#### 🛡️ Crittografia Tattica: Le "Frequenze" di Comunicazione
In Space GL, la crittografia non è solo sicurezza, ma una **scelta di frequenza operativa**. Ogni algoritmo agisce come una banda di comunicazione separata.

*   **Identità e Firma (Ed25519)**: Ogni pacchetto radio è firmato digitalmente. Se ricevi un messaggio con tag **`[VERIFIED]`**, hai la certezza matematica che provenga dal capitano dichiarato e che non sia stato alterato da sensori nemici o fenomeni spaziali.
*   **Frequenze Crittografiche (`enc <TIPO>`)**:
    *   **AES (`enc aes`)**: Lo standard di flotta bilanciato. Sicuro e ottimizzato per l'hardware moderno.
    *   **PQC (`enc pqc`)**: **Crittografia Post-Quantistica (ML-KEM)**. Rappresenta la difesa definitiva contro i computer quantistici dei Swarm o di civiltà temporalmente avanzate. È il protocollo più sicuro disponibile.
    *   **ChaCha (`enc chacha`)**: Ultra-veloce, ideale per comunicazioni rapide in condizioni di instabilità Dello spazio profondo.
    *   **Camellia (`enc camellia`)**: Protocollo standard dell'Impero Xylario, ottimizzato per resistere ad attacchi di forza bruta.
    *   **ARIA (`enc aria`)**: Standard utilizzato dall'Alleanza Korthian per le operazioni di coalizione.
    *   **IDEA / CAST5**: Protocolli spesso utilizzati da gruppi di resistenza (Maquis) o mercenari per sfuggire al monitoraggio standard della Flotta.
    *   **OFF (`enc off`)**: Comunicazione in chiaro. Rischiosa, ma utile per chiamate di soccorso universali leggibili da chiunque.

**Implicazione Tattica**: Se una flotta alleata decide di operare su "Frequenza ARIA", ogni membro deve impostare `enc aria`. Chi rimane su AES vedrà solo rumore statico (**`<< SIGNAL DISTURBED >>`**), permettendo comunicazioni sicure e segrete anche in settori affollati.
```bash
./run_server.sh
```

### 4. Avvio del Client
In un altro terminale, lancia il client:
```bash
./run_client.sh
```
**Flusso di accesso:**
1.  **Server IP:** Inserisci l'indirizzo del server.
2.  **Handshake:** Il client valida la Master Key. Se errata, esce immediatamente per sicurezza.
3.  **Identificazione:** Solo se il link è sicuro, ti verrà chiesto il **Commander Name**.
4.  **Configurazione:** Se è la tua prima volta, sceglierai Fazione e Classe della nave.

---

## ⚙️ Configurazione Avanzata e Modding

Il gameplay è interamente personalizzabile tramite il file di configurazione centralizzato **`include/game_config.h`**.
Modificando questo file e ricompilando con `make`, è possibile alterare le regole della fisica e del combattimento del proprio server.

Parametri configurabili includono:
*   **Limiti Risorse:** `MAX_ENERGY_CAPACITY`, `MAX_TORPEDO_CAPACITY`.
*   **Bilanciamento Danni:** `DMG_Ion Beam_BASE` (potenza Raggio Ionico), `DMG_TORPEDO` (danno siluri).
*   **Distanze di Interazione:** `DIST_MINING_MAX` (raggio minerario), `DIST_BOARDING_MAX` (raggio teletrasporto per arrembaggio).

| Operazione | Comando | Distanza Massima (Settore) |
| :--- | :--- | :--- |
| **Raccolta Plasma Reserves** | `har` | **3.1** |
| **Estrazione Mineraria** | `min` | **3.1** |
| **Ricarica Solare** | `sco` | **3.1** |
| **Arrembaggio / Squadre** | `bor` | **1.0** |
| **Attracco Base** | `doc` | **3.1** |
| **Recupero Sonde** | `aux recover` | **3.1** |

*Nota: La distanza di sicurezza operativa dai Buchi Neri è fissata a **3.0**. Oltrepassando questa soglia si entra nel pozzo gravitazionale (attrazione fisica e drenaggio scudi). Il raggio di interazione di 3.1 permette operazioni di raccolta sicure appena fuori dal limite gravitazionale. È implementata una tolleranza di **0.05 unità** per compensare le imprecisioni decimali dell'autopilota.*

*   **Eventi:** `TIMER_SUPERNOVA` (durata del countdown catastrofico).

Questo permette agli amministratori di creare varianti del gioco (es. *Hardcore Survival* con poche risorse o *Arcade Deathmatch* con armi potenziate).

---

### 🔐 Architettura di Sicurezza: Protocollo "Dual-Layer"

Space GL implementa un modello di sicurezza di livello militare, progettato per garantire la segretezza delle comunicazioni anche in ambienti multi-squadra ostili.

#### 1. Il Concetto
Il sistema utilizza due livelli di crittografia distinti per bilanciare accessibilità e isolamento tattico:

1.  **Master Key (Shared Secret - KEK):**
    *   **Ruolo:** Funziona come **Key Encryption Key (KEK)**. Non cifra direttamente i dati di gioco, ma protegge il tunnel in cui viene scambiata la chiave di sessione.
    *   **Verifica di Integrità:** Il sistema utilizza una "Firma Magica" di 32 byte (`HANDSHAKE_MAGIC_STRING`). Durante l'aggancio, il server tenta di decifrare questa firma usando la Master Key fornita.
    *   **Rigore Tattico:** Se anche un solo bit della Master Key differisce (es. "ciao" vs "ciao1"), la firma risulterà corrotta. Il server rileverà l'anomalia e **troncherà istantaneamente la connessione TCP**, emettendo un `[SECURITY ALERT]` nei log.
    *   **Impostazione:** Viene richiesta all'avvio dagli script `run_server.sh` e `run_client.sh`.

2.  **Session Key (Unique Ephemeral Key):**
    *   **Ruolo:** Chiave crittografica casuale a 256-bit generata dal client per ogni sessione.
    *   **Isolamento Totale:** Una volta convalidata la Master Key, il server e il client passano alla Session Key. Questo garantisce che **ogni giocatore/squadra sia sintonizzato su una frequenza crittografica diversa**, rendendo i dati di una squadra inaccessibili alle altre anche se condividono lo stesso server.

#### 2. Guida all'Avvio Sicuro

Per garantire che la sicurezza sia attiva, utilizzare sempre gli script bash forniti invece di lanciare direttamente gli eseguibili.

**Avvio del Server:**
```bash
./run_server.sh
# Ti verrà chiesto di inserire una Master Key segreta (es. "DeltaVega47").
# Questa chiave dovrà essere comunicata a tutti i giocatori autorizzati.
```

**Avvio del Client:**
```bash
./run_client.sh
# Inserisci la STESSA Master Key impostata sul server.
# Il sistema confermerà: "Secure Link Established. Unique Frequency active."
```

#### 3. Comandi di Crittografia in Gioco
Una volta connessi, la sicurezza è attiva ma trasparente. I capitani possono scegliere l'algoritmo di cifratura tattica (il "sapore" della crittografia) usando il comando `enc`:

*   `enc aes`: Attiva AES-256-GCM (Standard della Flotta).
*   `enc chacha`: Attiva ChaCha20-Poly1305 (Alta velocità).
*   `enc off`: Disattiva la cifratura (Traffico in chiaro, sconsigliato).

*Nota: Se due giocatori usano algoritmi diversi (es. uno AES e l'altro ChaCha), non potranno leggere i messaggi radio l'uno dell'altro, vedendo solo "rumore statico". Questo obbliga le squadre a coordinare le frequenze di comunicazione.*

---

## 🛠️ Architettura del Sistema e Dettagli Costruttivi

Il gioco è basato sull'architettura **Deep Space-Direct Bridge (SDB)**, un modello di comunicazione ibrido progettato per eliminare i colli di bottiglia tipici dei simulatori multiplayer in tempo reale.

### Il Modello Deep Space-Direct Bridge (SDB)
Questa architettura d'avanguardia risolve il problema della latenza e del jitter tipici dei giochi multiplayer intensivi, disaccoppiando completamente la sincronizzazione della rete dalla fluidità della visualizzazione. Il modello SDB trasforma il client in un **Relè Tattico Intelligente**, ottimizzando il traffico remoto e azzerando la latenza locale.

1.  **Deep Space Channel (TCP/IP Binary Link)**:
    *   **Ruolo**: Sincronizzazione autoritativa dello stato galattico.
    *   **Tecnologia**: Protocollo binario proprietario con **Interest Management** dinamico. Il server calcola quali oggetti sono visibili al giocatore e invia solo i dati necessari, riducendo l'uso della banda fino all'85%.
    *   **Caratteristiche**: Implementa pacchetti a lunghezza variabile e packing binario (`pragma pack(1)`) per eliminare il padding e massimizzare l'efficienza sui canali remoti.

2.  **Direct Bridge (POSIX Shared Memory Link)**:
    *   **Ruolo**: Interfaccia a latenza zero tra logica locale e motore grafico.
    *   **Tecnologia**: Segmento di memoria condivisa (`/dev/shm`) mappato direttamente negli spazi di indirizzamento di Client e Visualizzatore.
    *   **Efficienza**: Utilizza un approccio **Zero-Copy**. Il Client scrive i dati ricevuti dal server direttamente nella SHM; il Visualizzatore 3D li consuma istantaneamente. La sincronizzazione è garantita da semafori POSIX e mutex, permettendo al motore grafico di girare a 60+ FPS costanti, applicando **Linear Interpolation (LERP)** per compensare i buchi temporali tra i pacchetti di rete.

#### 🔄 Pipeline di Flusso del Dato (Propagazione Tattica)
L'efficacia del modello SDB è visibile osservando il viaggio di un singolo aggiornamento (es. il movimento di un Falco da Guerra Xylario):
1.  **Server Tick (Logic)**: Il server calcola la nuova posizione globale del nemico e aggiorna l'indice spaziale.
2.  **Deep Space Pulse (Network)**: Il server serializza il dato nel `PacketUpdate`, lo tronca per includere solo gli oggetti nel quadrante del giocatore e lo invia via TCP.
3.  **Client Relay (Async)**: Il thread `network_listener` del client riceve il pacchetto, valida il `Frame ID` e scrive le coordinate nella **Shared Memory**.
4.  **Direct Bridge Signal (IPC)**: Il client incrementa il semaforo `data_ready`.
5.  **Viewer Wake-up (Rendering)**: Il visualizzatore esce dallo stato di *wait*, acquisisce il mutex, copia le nuove coordinate come `target` e avvia il calcolo LERP per far scivolare fluidamente il vascello verso la nuova posizione durante i successivi frame grafici.

Grazie a questa pipeline, i comandi via terminale viaggiano nel "Subspazio" con la sicurezza del protocollo TCP, mentre la vista tattica sul ponte rimane stabile, fluida e priva di scatti, indipendentemente dalla qualità della connessione internet.

### 1. Il Server Galattico (`stellar_server`)
È il "motore" del gioco. Gestisce l'intero universo di 1000 quadranti.
*   **Logica Modulare**: Diviso in moduli (`galaxy.c`, `logic.c`, `net.c`, `commands.c`) per garantire manutenibilità e thread-safety.
*   **Configurazione Centralizzata**: Tutti i parametri di bilanciamento (distanze di mining, danni delle armi, raggio delle boe e delle nebulose) sono ora riuniti in `include/game_config.h`. Questo permette agli amministratori di modificare l'intera fisica del gioco da un unico punto.
*   **Sicurezza e Stabilità**: Il codice è stato revisionato per eliminare rischi di Buffer Overflow tramite l'uso sistematico di `snprintf` e la gestione dinamica dei buffer tattici.
*   **Spatial Partitioning**: Utilizza un indice spaziale 3D (Grid Index) per la gestione degli oggetti.
*   **Persistenza**: Salva lo stato dell'intero universo, inclusi i progressi dei giocatori, in `galaxy.dat` con controllo di versione binaria.

### 2. Il Ponte di Comando (`stellar_client`)
Il `stellar_client` rappresenta il nucleo operativo dell'esperienza utente, agendo come un sofisticato orchestratore tra l'operatore umano, il server remoto e il motore di rendering locale.

*   **Architettura Multi-Threaded**: Il client gestisce simultaneamente diverse pipeline di dati:
    *   Un thread dedicato (**Network Listener**) monitora costantemente il *Deep Space Channel*, processando i pacchetti in arrivo dal server senza bloccare l'interfaccia.
    *   Il thread principale gestisce l'input utente e il feedback immediato sul terminale.
*   **Gestione Input Reattivo (Reactive UI)**: Grazie all'uso della modalità `raw` di `termios`, il client intercetta i singoli tasti in tempo reale. Questo permette al giocatore di ricevere messaggi radio, avvisi del computer e aggiornamenti tattici *mentre sta scrivendo* un comando, senza che il cursore o il testo vengano interrotti o sporcati.
*   **Orchestrazione Direct Bridge**: Il client è responsabile del ciclo di vita della memoria condivisa (SHM). All'avvio, crea il segmento di memoria, inizializza i semafori di sincronizzazione e lancia il processo `stellar_3dview`. Ogni volta che riceve un `PacketUpdate` dal server, il client aggiorna istantaneamente la matrice di oggetti nella SHM, notificando il visualizzatore tramite segnali POSIX.
*   **Identity & Persistence Hub**: Gestisce la procedura di login e la selezione della fazione/classe, interfacciandosi con il database persistente del server per ripristinare lo stato della missione.

In sintesi, il `stellar_client` trasforma un semplice terminale testuale in un ponte di comando avanzato e fluido, tipico di un'interfaccia GDIS.

### 3. Il Visualizzatore Galattico (`spacegl_viewer`)
Il `spacegl_viewer` è uno strumento diagnostico a basso livello progettato per amministratori e giocatori avanzati, che permette di ispezionare lo stato della galassia persistente salvata in `galaxy.dat`.

*   **Ispezione Offline**: A differenza del visualizzatore 3D, che richiede server e client attivi, `spacegl_viewer` legge direttamente il file binario `galaxy.dat`.
*   **Monitoraggio Sicurezza**: Fornisce un report dettagliato sullo stato crittografico della galassia, inclusa la verifica della firma HMAC-SHA256 e i flag di cifratura attivi.
*   **Statistiche Dettagliate**: Mostra i conteggi globali per tutte le 17 classi di oggetti (NPC, Stelle, Pianeti, Mostri, ecc.) e le metriche dei giocatori.
*   **Comandi Astrometrici**:
    *   `stats`: Censimento completo della galassia e rapporto di sicurezza.
    *   `map <q3>`: Genera una sezione 2D ASCII della galassia a una specifica profondità Z (1-10), mostrando la distribuzione di nebulose, rift, piattaforme e stelle.
    *   `list <q1> <q2> <q3>`: Fornisce un censimento millimetrico di un singolo quadrante, rivelando coordinate precise, tipi di risorse (es. Dilithio, Tritanium) e classi di navi per tutte le entità.
    *   `players`: Elenca tutti i comandanti persistenti, il loro settore attuale, lo stato di occultamento e la frequenza crittografica attiva.
    *   `search <name>`: Esegue una ricerca ricorsiva per localizzare un capitano o un vascello specifico nell'intero universo di 1000 quadranti.

### 4. La Vista Tattica 3D (`stellar_3dview`)
Il visualizzatore 3D è un motore di rendering standalone basato su **OpenGL e GLUT**, progettato per fornire una rappresentazione spaziale immersiva dell'area tattica circostante e dell'intera galassia.

*   **Esperienza Widescreen (16:9)**: La finestra di visualizzazione è ottimizzata per il formato 1280x720, offrendo un campo visivo cinematografico.
    *   **FOV Dinamico**: Il sistema adatta automaticamente l'angolo di visuale (Field of View): fissa a 45° in modalità tattica per la massima precisione di manovra, e grandangolare a 65° in modalità Ponte per un'immersione totale.
    *   **Bussola AR Dinamica (Augmented Reality)**: Il sistema di puntamento `axs` ora implementa una suite di navigazione inerziale avanzata:
        *   **Heading Ring Inclinabile**: L'anello della bussola orizzontale è ora solidale al piano di volo della nave, inclinandosi con il beccheggio per mantenere il riferimento direzionale costante nel campo visivo del pilota.
        *   **Arco del Mark Verticale**: L'arco dei gradi verticali rimane ancorato allo zenit galattico, permettendo di leggere l'inclinazione effettiva della nave (salita/discesa) mentre la prua scorre lungo i gradi dell'arco.
        *   **Assi Galattici Fissi**: Gli assi X, Y, Z rimangono punti di riferimento assoluti non influenzati dal movimento della nave.
    *   **Fluidità Cinematica (LERP)**: Per ovviare alla natura discreta dei pacchetti di rete, il motore implementa algoritmi di **Linear Interpolation (LERP)** sia per le posizioni che per gli orientamenti (Heading/Mark). Gli oggetti non "saltano" da un punto all'altro, ma scivolano fluidamente nello spazio, mantenendo i 60 FPS anche se il server aggiorna la logica a frequenza inferiore.
*   **Rendering ad Alte Prestazioni**: Utilizza **Vertex Buffer Objects (VBO)** per gestire migliaia di stelle di sfondo e la griglia galattica, minimizzando le chiamate alla CPU e massimizzando il throughput della GPU.
*   **Cartografia Stellare (Modalità Mappa)**:
    *   Attivabile tramite il comando `map`, questa modalità attiva una transizione cinematografica che "sgancia" la telecamera tattica dalla nave per offrire una **proiezione olografica tridimensionale** dell'intero settore.
    *   **Rendering a 64-bit**: Grazie alla codifica BPNBS estesa, la mappa visualizza con precisione assoluta la densità e la tipologia di ogni corpo celeste, senza errori di arrotondamento.
    *   **Legenda Olografica Oggetti**:
        *   🚀 **Giocatore** (Ciano): La tua nave.
        *   ☀️ **Stella** (Giallo): Classe spettrale variabile.
        *   🪐 **Pianeta** (Ciano): Risorse minerarie o abitabili.
        *   🛰️ **Base Stellare** (Verde): Porto sicuro per riparazioni.
        *   🕳️ **Buco Nero** (Viola): Singolarità gravitazionale.
        *   🌫️ **Nebulosa** (Grigio): Nube di gas (interferenza sensori).
        *   ✴️ **Pulsar** (Arancione): Stella di neutroni (radiazioni).
        *   ☄️ **Cometa** (Celeste): Corpo ghiacciato in orbita eccentrica.
        *   🪨 **Asteroide** (Marrone): Campo detriti navigabile.
        *   🛸 **Relitto** (Grigio Scuro): Nave abbandonata da smantellare.
        *   💣 **Mina** (Rosso): Ordigno di prossimità.
        *   📍 **Boa** (Blu): Transponder di navigazione.
        *   🛡️ **Piattaforma** (Arancione Scuro): Difesa statica automatizzata.
        *   🌀 **Rift** (Ciano): Anomalia spaziale instabile (teletrasporto).
        *   👾 **Mostro Spaziale** (Bianco Pulsante): Minaccia di classe Omega.
        *   ⚡ **Tempesta Ionica** (Bianco Wireframe): Perturbazione energetica locale.
    *   Le **tempeste ioniche** attive sono visualizzate come gusci energetici bianchi che avvolgono il quadrante.
    *   La posizione attuale del giocatore è evidenziata da un **indicatore bianco pulsante**, facilitando la navigazione a lungo raggio.
*   **HUD Tattico Dinamico**: Implementa una proiezione 2D-su-3D (via `gluProject`) per ancorare etichette, barre della salute e identificativi direttamente sopra i vascelli. L'overlay include ora il monitoraggio in tempo reale dell'**Equipaggio (CREW)**, vitale per la sopravvivenza della missione.
*   **Engine degli Effetti (VFX)**:
    *   **Trail Engine**: Ogni nave lascia una scia ionica persistente che aiuta a visualizzarne il vettore di movimento.
    *   **Combat FX**: Visualizzazione in tempo reale di raggi Ion Beam gestiti via **GLSL Shader**, torpedini fotoniche con bagliore dinamico ed esplosioni volumetriche.
    *   **Dismantle Particles**: Un sistema particellare dedicato anima lo smantellamento dei relitti nemici durante le operazioni di recupero risorse.
*   **Cubo Tattico 3D**: La griglia wireframe che avvolge il settore è ora allineata verticalmente ai livelli di profondità (S3) del comando `lrs`:
    *   🟩 **Piano Superiore (Verde)**: Corrisponde a `[ LONG RANGE DEPTH +1 ]` (Quota superiore).
    *   🟨 **Centro (Giallo)**: Corrisponde a `[ LOCAL TACTICAL ZONE 0 ]` (Tua quota attuale).
    *   🟥 **Piano Inferiore (Rosso)**: Corrisponde a `[ LONG RANGE DEPTH -1 ]` (Quota inferiore).
    *   Questa mappatura permette di identificare istantaneamente la posizione verticale degli oggetti.

La Vista Tattica non è solo un elemento estetico, ma uno strumento fondamentale per il combattimento a corto raggio e la navigazione di precisione tra corpi celesti e minacce ambientali.

---

## 📡 Protocolli di Comunicazione

### Rete (Server ↔ Client): Il Deep Space Channel
La comunicazione remota è affidata a un protocollo binario state-aware personalizzato, progettato per garantire coerenza e prestazioni su reti con latenza variabile.

*   **Protocollo Binario Deterministico**: A differenza dei protocolli testuali (come JSON o XML), il Deep Space Channel utilizza un'architettura **Binary-Only**. Le strutture dati sono allineate tramite `pragma pack(1)` per eliminare il padding del compilatore, garantendo che ogni byte trasmesso sia un'informazione utile.
*   **State-Aware Synchronization**:
    *   Il server non si limita a inviare posizioni, ma sincronizza l'intero stato logico necessario al client (energia, scudi, inventario, messaggi del computer di bordo).
    *   Ogni pacchetto di aggiornamento (`PacketUpdate`) include un **Frame ID** globale, permettendo al client di gestire correttamente l'ordine temporale dei dati.
*   **Interest Management & Delta Optimization**:
    *   **Filtraggio Spaziale**: Il server calcola dinamicamente il set di visibilità di ogni giocatore. Riceverai dati solo sugli oggetti presenti nel tuo quadrante attuale o che influenzano i tuoi sensori a lungo raggio, riducendo drasticamente il carico di rete.
    *   **Truncated Updates**: I pacchetti che contengono liste di oggetti (come navi nemiche o detriti) vengono troncati fisicamente prima dell'invio. Se nel tuo quadrante ci sono solo 2 navi, il server invierà un pacchetto contenente solo quei 2 slot invece dell'intero array fisso, risparmiando KB preziosi ad ogni tick.
*   **Data Integrity & Stream Robustness**:
    *   Implementa un meccanismo di **Atomic Read/Write**. Le funzioni `read_all` e `write_all` garantiscono che, nonostante la natura "stream" del TCP, i pacchetti binari vengano ricostruiti solo quando sono completi e integri, prevenendo la corruzione dello stato logico durante picchi di traffico.
*   **Multiplexing del Segnale**: Il protocollo gestisce diversi tipi di pacchetti (`Login`, `Command`, `Update`, `Message`, `Query`) sullo stesso socket, agendo come un multiplexer di segnale Dello spazio profondo.

Questa implementazione permette al simulatore di scalare fluidamente, mantenendo la latenza di comando (Input Lag) minima e la coerenza della galassia assoluta per tutti i capitani connessi.

### IPC (Client ↔ Visualizzatore): Il Direct Bridge
Il link tra il ponte di comando e la vista tattica è realizzato tramite un'interfaccia di comunicazione inter-processo (IPC) basata su **POSIX Shared Memory**, progettata per eliminare la latenza di scambio dati locale.

*   **Architettura Shared-Memory**: Il `stellar_client` alloca un segmento di memoria dedicato (`/st_shm_PID`) in cui risiede la struttura `GameState`. Questa struttura funge da rappresentazione speculare dello stato locale, accessibile in tempo reale sia dal client (scrittore) che dal visualizzatore (lettore).
*   **Sincronizzazione Ibrida (Mutex & Semaphores)**:
    *   **PTHREAD_PROCESS_SHARED Mutex**: La coerenza dei dati all'interno della memoria condivisa è garantita da un mutex configurato per l'uso tra processi diversi. Questo impedisce al visualizzatore di leggere dati parziali mentre il client sta aggiornando la matrice degli oggetti.
    *   **POSIX Semaphores**: Un semaforo (`sem_t data_ready`) viene utilizzato per implementare un meccanismo di notifica di tipo "Producer-Consumer". Invece di interrogare costantemente la memoria (polling), il visualizzatore rimane in uno stato di attesa efficiente finché il client non segnala la disponibilità di un nuovo frame logico.
*   **Zero-Copy Efficiency**: Poiché i dati risiedono fisicamente nella stessa area della RAM mappata in entrambi gli spazi di indirizzamento, il passaggio dei parametri telemetrici non comporta alcuna copia di memoria (memcpy) aggiuntiva, massimizzando le prestazioni del bus di sistema.
*   **Latching degli Eventi**: La SHM gestisce il "latching" di eventi rapidi (come esplosioni o scariche Ion Beam). Il client deposita l'evento nella memoria e il visualizzatore, dopo averlo renderizzato, provvede a resettare il flag, garantendo che nessun effetto tattico venga perso o duplicato.
*   **Orchestrazione del Ciclo di Vita**: Il client funge da supervisore, gestendo la creazione (`shm_open`), il dimensionamento (`ftruncate`) e la distruzione finale della risorsa IPC, assicurando che il sistema non lasci orfani di memoria in caso di crash.

Questo approccio trasforma il visualizzatore 3D in un puro slave grafico reattivo, permettendo al motore di rendering di concentrarsi esclusivamente sulla fluidità visiva e sul calcolo geometrico.

---

## 🔍 Specifiche Tecniche

Space GL non è solo un simulatore tattico, ma un'architettura software complessa che implementa pattern di design avanzati per la gestione dello stato distribuito e il calcolo real-time.

### 1. Il Synaptics Logic Engine (Tick-Based Simulation)
Il server opera su un loop deterministico a **30 Tick Per Second (TPS)**. Ogni ciclo logico segue una pipeline rigorosa:
*   **Input Reconciliation**: Processamento dei comandi atomici ricevuti dai client via epoll.
*   **Skirmish AI (Tattica Avanzata)**: Gli NPC non si limitano più a un inseguimento lineare. Utilizzano una logica a stati "Attack Run":
    *   **Corsa d'Attacco**: Selezionano un vettore casuale nel settore per disorientare i sensori.
    *   **Posizionamento**: Raggiungono il punto di fuoco ottimale.
    *   **Fuoco di Saturazione**: Ruotano la prua verso il giocatore e scaricano i Ion Beam per 4 secondi.
    *   **Riposizionamento**: Calcolano immediatamente una nuova traiettoria evasiva.
*   **Spatial Indexing (Grid Partitioning)**: Gli oggetti non vengono iterati linearmente ($O(N)$), ma mappati in una griglia tridimensionale 10x10x10. Questo riduce la complessità delle collisioni e dei sensori a $O(1)$ per l'area locale del giocatore.
*   **Physics Enforcement**: Applicazione del clamping galattico e risoluzione delle collisioni con corpi celesti statici.

### 2. ID-Based Object Tracking & Shared Memory Mapping
Il sistema di tracciamento degli oggetti utilizza un'architettura a **Identificativi Persistenti**:
*   **Server Side**: Ogni entità (nave, stella, pianeta) ha un ID univoco globale. Durante il tick, solo gli ID visibili al giocatore vengono serializzati.
*   **Client/Viewer Side**: Il visualizzatore mantiene un buffer locale di 200 slot. Attraverso una **Hash Map implicita**, il client associa l'ID del server a uno slot SHM. Se un ID scompare dal pacchetto di rete, il sistema di *Stale Object Purge* invalida istantaneamente lo slot locale, garantendo la coerenza visiva senza latenza di timeout.

### 3. Modello di Networking Avanzato (Atomic Binary Stream)
Per garantire la stabilità su reti ad alta frequenza, il simulatore implementa:
*   **Atomic Packet Delivery:** Ogni connessione client è protetta da un `socket_mutex` dedicato. Questo garantisce che i pacchetti di grandi dimensioni (come la Galassia Master) non vengano mai interrotti o mescolati con i pacchetti di aggiornamento logico, eliminando alla radice la corruzione del flusso binario (Race Conditions).
*   **Binary Layout Consistency:** Tutte le strutture di rete utilizzano esclusivamente tipi a dimensione fissa (`int32_t`, `int64_t`, `float`). Combinato con `pragma pack(1)`, questo assicura che il protocollo sia identico tra diverse architetture CPU e versioni del compilatore.
*   **Synchronous Security Handshake:** Il collegamento non viene stabilito "alla cieca". Il server esegue una validazione sincrona della firma a 32 byte prima di inviare dati sensibili, garantendo un firewall logico impenetrabile.

### 4. Gestione Memoria e Stabilità del Server
*   **Heap-Based Command Processing:** Le funzioni di scansione sensoriale (`srs`, `lrs`) utilizzano allocazione dinamica nell'Heap (`calloc`/`free`) per gestire buffer di dati superiori a 64KB. Questo previene rischi di *Stack Overflow*, garantendo che il server rimanga stabile anche in quadranti ad altissima densità di oggetti.
*   **Zero-Copy Shared Memory (Riallineata):** La SHM utilizza ora coordinate di settore esplicite (`shm_s`) per il posizionamento del giocatore, eliminando la latenza di ricerca ID nel visore 3D e garantendo un puntamento telecamera millimetrico.

### 5. Rendering Pipeline GLSL (Hardware-Accelerated Aesthetics)
Il visualizzatore 3D implementa una pipeline di ombreggiatura programmabile:
*   **Vertex Stage**: Gestione delle trasformazioni di proiezione HUD e calcolo dei vettori di luce per-pixel.
*   **Fragment Stage**: 
    *   **Aztek Shader**: Generazione procedurale di texture di scafo basate su coordinate di frammento, eliminando la necessità di asset grafici esterni.
    *   **Fresnel Rim Lighting**: Calcolo del dot product tra normale e vettore di vista per evidenziare i profili strutturali dei vascelli.
    *   **Plasma Flow Simulation**: Animazione temporale dei parametri emissivi per simulare lo scorrimento di energia nelle gondole Hyperdrive.

### 5. Robustezza e Session Continuity
*   **Atomic Save-State**: Il database `galaxy.dat` viene aggiornato tramite flush periodici con lock del mutex globale, assicurando uno snapshot coerente della memoria.
*   **Emergency Rescue Protocol**: Una logica di salvataggio euristica interviene al login per risolvere stati di errore (collisioni o navi distrutte), garantendo la persistenza della carriera del giocatore anche in caso di fallimento della missione.

---

## 🌌 Enciclopedia Galattica: Entità e Fenomeni

L'universo di Space GL è un ecosistema dinamico popolato da 17 classi di entità, ognuna con proprietà fisiche, tattiche e visive uniche.

### 🌟 Corpi Celesti e Fenomeni Astronomici
*   **Stelle (Stars)**: Classificate in 7 tipi spettrali (O, B, A, F, G, K, M). Forniscono energia tramite *Solar Scooping* (`sco`) ma possono diventare instabili e innescare una **Supernova**.
    *   **Frequenza**: Eventi cataclismatici sincronizzati globalmente (circa ogni **5 minuti** di gioco).
    *   **Allerta**: Un conto alla rovescia appare nell'HUD quando una stella è prossima all'esplosione.
    *   **Impatto**: Distruzione totale di ogni vascello nel quadrante allo scadere del tempo.
    *   **Eredità**: La stella viene sostituita da un **Buco Nero** permanente, alterando per sempre la mappa galattica.
*   **Pianeti**: Corpi celesti ricchi di minerali.
*   **Buchi Neri (Black Holes)**: Singolarità gravitazionali con dischi di accrezione. Sono la fonte primaria di Plasma Reserves (`har`), ma la loro attrazione può essere fatale.
*   **Nebulose (Nebulas)**: Grandi nubi di gas ionizzati (Classi Mutara, Metreon, Dark Matter Cloud, ecc.). Forniscono copertura tattica (occultamento naturale) ma disturbano i sensori.
*   **Pulsar**: Stelle di neutroni che emettono radiazioni letali. Navigare troppo vicino danneggia i sistemi e l'equipaggio.
*   **Comete (Comets)**: Oggetti in movimento veloce con code volumetriche. Possono essere analizzate per raccogliere gas rari.
*   **Faglie Spaziali (Spatial Rifts)**: Strappi nel tessuto spaziotemporale. Fungono da teletrasporti naturali che proiettano la nave in un punto casuale della galassia.

### 🚩 Fazioni e Navi Intelligenti
*   **Alleanza (Player/Starbase)**: Include la tua nave e le Basi Stellari, dove puoi attraccare (`doc`) per riparazioni e rifornimenti completi.
*   **Impero Korthian**: Guerrieri aggressivi che pattugliano i quadranti, spesso protetti da piattaforme difensive.
*   **Impero Stellare Xylario**: Maestri dell'inganno che utilizzano l'occultamento per lanciare attacchi a sorpresa.
*   **Collettivo Swarm**: La minaccia più grande. I loro Cubi hanno una potenza di fuoco massiccia e capacità rigenerative superiori.
*   **Fazioni NPC**: Vesperiani, Ascendant, Quarzitei, Saurian, Gilded, Specie 8472, Cryos e Apex. Ognuna con diversi livelli di ostilità e potenza.

#### ⚖️ Sistema di Fazioni e Protocollo "Traditore" (Renegade)
Space GL implementa un sistema di reputazione dinamico che gestisce le relazioni tra il giocatore e le diverse potenze galattiche.

*   **Riconoscimento IFF Alleato**: Al momento della creazione del personaggio, il capitano sceglie una fazione di appartenenza. Le navi NPC e le piattaforme di difesa della stessa fazione riconosceranno il vascello come alleato e **non apriranno il fuoco a vista**.
*   **Fuoco Amico e Tradimento**: Se un giocatore attacca deliberatamente un'unità della propria fazione (nave, base o piattaforma):
    *   **Stato Renegade**: Il capitano viene immediatamente marcato come **TRADITORE (Renegade)**.
    *   **Ritorsione Immediata**: Tutte le unità della propria fazione nel settore diventeranno ostili e inizieranno manovre di attacco pesanti.
    *   **Messaggio di Allerta**: Il computer di bordo riceverà un avviso critico dal Comando di Settore: `FRIENDLY FIRE DETECTED! You have been marked as a TRAITOR by the fleet!`.
*   **Durata e Amnistia**: Lo status di traditore è temporaneo ma severo. Ha una durata di **10 minuti (tempo reale)**.
    *   Durante questo periodo, ogni ulteriore attacco contro alleati resetterà il timer.
    *   Allo scadere del tempo, se non sono state commesse altre ostilità, il Comando di Settore concederà l'amnistia: `Amnesty granted. Your status has been restored to active duty.` e le unità della fazione torneranno ad essere neutrali/alleate.

### ⚠️ Pericoli e Risorse Tattiche
*   **Campi di Asteroidi**: Detriti rocciosi che rappresentano un rischio fisico. Il danno da collisione aumenta con la velocità della nave.
*   **Mine Spaziali**: Ordigni esplosivi occulti piazzati da fazioni ostili. Rilevabili solo tramite scansione ravvicinata.
*   **Relitti alla Deriva (Derelicts)**: Gusci di navi distrutte. Possono essere smantellati (`dis`) per recuperare componenti e risorse.
*   **Boe di Comunicazione (ID 15xxx)**:
    *   **Vantaggio**: Trovarsi vicino a una boa (**Distanza < 1.2**) potenzia i sensori a lungo raggio (`lrs`).
    *   **Effetto Tattico**: Invece di ricevere un semplice conteggio totale, la boa trasmette la composizione esatta dei quadranti adiacenti (es. `H:1 P:2` invece di un generico `3`).
    *   **Uso**: Utile per mappare territori ostili senza entrarvi o per localizzare basi stellari lontane.
*   **Piattaforme Difensive (Turrets)**: Sentinelle automatiche pesantemente armate che proteggono aree di interesse strategico.

### 👾 Anomalie e Creature
*   **Mostri Spaziali**: Include l'**Entità Cristallina** e l'**Ameba Spaziale**, creature uniche che cacciano attivamente i vascelli per nutrirsi della loro energia.
*   **Tempeste Ioniche**: Fenomeni meteorologici che si spostano nella galassia, capaci di accecare i sensori e deviare la rotta delle navi in transito.

---

## 🕹️ Manuale Operativo dei Comandi

Di seguito la lista completa dei comandi disponibili, raggruppati per funzione.

### 🚀 Navigazione
*   `nav <H> <M> <Dist> [Fattore]`: **Navigazione Hyperdrive ad Alta Precisione**. Imposta rotta, distanza precisa e velocità opzionale.
    *   `H`: Heading (0-359).
    *   `M`: Mark (-90 a +90).
    *   `Dist`: Distanza in Quadranti (supporta decimali, es. `1.73`).
    *   `Fattore`: (Opzionale) Fattore Hyperdrive da 1.0 a 9.9 (Default: 6.0).
*   `imp <H> <M> <S>`: **Impulse Drive**. Motori sub-luce. `S` rappresenta la velocità da 0.0 a 1.0 (Full Impulse).
    *   `S`: Speed (0.0 - 1.0).
    *   `imp 0 0 0`: Arresto motori (All Stop).
*   `cal <QX> <QY> <QZ> [SX SY SZ]`: **Computer di Navigazione (Alta Precisione)**. Calcola il vettore esatto e la distanza dalla posizione attuale verso la destinazione specificata (Quadrante + Settore opzionale). Include una **Tabella del Profilo Hyperdrive** con ETA sincronizzati.
*   `ical <X> <Y> <Z>`: **Impulse Calculator (ETA)**. Fornisce la soluzione di navigazione completa per coordinate di settore precise [0.0 - 10.0], incluso il tempo di percorrenza in tempo reale.
Line 353: *   `cal <QX> <QY> <QZ>`: **Hyperdrive Calculator**. Calcola H, M e tempo di arrivo stimato per raggiungere un quadrante distante.
    
    *   `jum <QX> <QY> <QZ>`: **Wormhole Jump (Einstein-Rosen Bridge)**.
     Genera un wormhole per un salto istantaneo verso il quadrante di destinazione.
    *   **Requisiti**: 5000 unità di Energia e 1 Cristallo di Dilithio.
    *   **Procedura**: Richiede una sequenza di stabilizzazione della singolarità di circa 3 secondi.
*   `apr [ID] [DIST]`: **Approach Autopilot**. Avvicinamento automatico al bersaglio ID fino a distanza DIST.
    *   Se non viene fornito un ID, utilizza il **bersaglio attualmente agganciato** (`lock`).
    *   Se viene fornito un solo numero, viene interpretato come **distanza** dal bersaglio agganciato (se < 100).
*   `cha`: **Chase Autopilot**. Insegue attivamente il bersaglio agganciato mantenendo la distanza di ingaggio.
*   `rad <MSG>`: **Deep Space Radio**. Invia un messaggio globale. Usa `@Fazione` per messaggi di squadra o `#ID` per messaggi privati.
*   `doc`: **Docking**. Attracco a una Base Stellare (richiede distanza ravvicinata).
*   `map [FILTRO]`: **Stellar Cartography**. Attiva la visualizzazione 3D globale 10x10x10 dell'intera galassia.
    *   **Filtri Opzionali**: È possibile visualizzare solo specifiche categorie usando: `map st` (Stelle), `map pl` (Pianeti), `map bs` (Basi), `map en` (Nemici), `map bh` (Buchi Neri), `map ne` (Nebulose), `map pu` (Pulsar), `map is` (Tempeste), `map co` (Comete), `map as` (Asteroidi), `map de` (Relitti), `map mi` (Mine), `map bu` (Boe), `map pf` (Piattaforme), `map ri` (Rift), `map mo` (Mostri).
    *   **HUD Verticale**: In modalità mappa, una legenda sul lato sinistro mostra i colori e i codici di filtro per ogni oggetto.
    *   **Anomalie Dinamiche**:
        *   **Tempeste Ioniche**: Quadranti avvolti da una gabbia bianca trasparente.
        *   **Supernova**: Un **cubo rosso gigante pulsante** indica un'imminente esplosione stellare nel quadrante (pericolo estremo).
    *   **Localizzazione**: La posizione attuale della nave è indicata da un **cubo bianco pulsante**.

### 🔬 Sensori e Scanner
*   `scan <ID>`: **Deep Scan Analysis**. Esegue una scansione profonda del bersaglio o dell'anomalia.
    *   **Navi**: Rivela integrità scafo, livelli scudi per quadrante, energia residua, equipaggio e danni ai sottosistemi.
    *   **Anomalie**: Fornisce dati scientifici su Nebulose e Pulsar.

#### 📡 Integrità dei Sensori e Precisione Dati
L'efficacia dei sensori dipende direttamente dalla salute del sistema **Sensori (ID 2)**:
*   **Salute 100%**: Dati precisi e affidabili.
*   **Salute < 100%**: Introduzione di "rumore" telemetrico. Le coordinate di settore `[X,Y,Z]` mostrate in `srs` diventano imprecise (l'errore aumenta esponenzialmente al calare della salute).
*   **Salute < 50%**: Il comando `lrs` inizia a mostrare dati corrotti o incompleti sui quadranti circostanti.
*   **Salute < 30%**: Rischio di "Target Ghosting" o mancata rilevazione di oggetti reali.
*   **Riparazione**: Usare `rep 2` per ripristinare la precisione nominale.

*   `srs`: **Short Range Sensors**. Scansione dettagliata del quadrante attuale.
    *   **Neighborhood Scan**: Se la nave è vicino ai confini del settore (< 2.5 unità), i sensori rilevano automaticamente oggetti nei quadranti adiacenti, elencandoli in una sezione dedicata per prevenire imboscate.
*   `lrs`: **Long Range Sensors**. Scansione 3x3x3 dei quadranti circostanti visualizzata tramite **Console Tattica GDIS**.
    *   **Layout**: Ogni quadrante è visualizzato su una singola riga per una lettura immediata (Coordinate, Navigazione, Oggetti e Anomalie).
    *   **Dato Standard**: Mostra solo la presenza di oggetti tramite le loro iniziali (es. `[H . N . S]`).
    *   **Dato Potenziato**: Se la nave è in prossimità di una **Boa di Comunicazione** (< 1.2 unità), i sensori passano alla visualizzazione numerica rivelando il conteggio esatto (es. `[1 . 2 . 8]`). Il potenziamento si resetta allontanandosi dalla boa.
    *   **Soluzione di Navigazione**: Ogni quadrante include i parametri `H / M / W` calcolati per raggiungerlo immediatamente.
    *   **Legenda Primaria**: `[ H P N B S ]` (Buchi Neri, Pianeti, Navi, Basi, Stelle).
    *   **Simbologia Anomalie**: `~`:Nebulosa, `*`:Pulsar, `+`:Cometa, `#`:Asteroide, `M`:Mostro, `>`:Rift.
    *   **Localizzazione**: Il proprio quadrante è evidenziato da uno sfondo blu.
*   `aux probe <QX> <QY> <QZ>`: **Sonda Sensore Dello spazio profondo**. Lancia una sonda automatizzata verso un quadrante specifico.
    *   **Entità Galattica**: Le sonde sono oggetti globali. Attraversano i quadranti in tempo reale e sono **visibili a tutti i giocatori** lungo la rotta.
    *   **Integrazione Sensori**: Le sonde appaiono nella lista **SRS (Short Range Sensors)** di chiunque si trovi nello stesso settore (ID range 19000+), rivelando il **Nome del Proprietario** e lo stato attuale.
    *   **Funzionamento**: Visualizza l'**ETA** e lo stato della missione nell'HUD del proprietario.
    *   **Recupero Dati**: All'arrivo, rivela la composizione del quadrante (`H P N B S`) nella mappa e invia un rapporto telemetrico.
    *   **Persistenza**: Rimane nel quadrante bersaglio come **Relitto (Derelict)** (anelli rossi) a fine missione.
    *   **Comando `aux report <1-3>`**: Interroga nuovamente una sonda attiva per ricevere un rapporto aggiornato.
    *   **Comando `aux recover <1-3>`**: Recupera una sonda se la nave si trova nello stesso quadrante e a distanza ravvicinata (< 2.0 unità), liberando lo slot e recuperando 500 unità di energia.
Line 385:     *   **Persistenza**: Dopo aver completato la missione, la sonda rimane nel quadrante bersaglio come **Relitto (Derelict)**, visibile in 3D come un oggetto grigio e inattivo.
*   `sta`: **Status Report**. Rapporto completo stato nave, missione e monitoraggio dell'**Equipaggio**.
*   `dam`: **Damage Report**. Dettaglio danni ai sistemi.
*   `who`: Lista dei capitani attivi nella galassia.

### ⚔️ Combattimento Tattico
*   `pha <E>`: **Fire Ion Beams**. Spara Ion Beam sul bersaglio agganciato (`lock`) con energia E. 
*   `pha <ID> <E>`: Spara Ion Beam su uno specifico bersaglio ID. Il danno diminuisce con la distanza.
*   `cha`: **Chase**. Insegue e intercetta automaticamente il bersaglio agganciato.
*   `rad <MSG>`: **Radio**. Invia un messaggio Dello spazio profondo agli altri capitani (@Fazione per chat di squadra).
*   `axs` / `grd`: **Guide Visive**. Attiva/disattiva gli assi 3D o la griglia tattica.
*   `bridge [top/bottom/up/down/left/right/rear/off]`: **Vista Ponte**. Attiva una sequenza cinematografica che sposta la camera sul ponte di comando (o sotto lo scafo).
    *   `top/on`: Visuale standard sopra la cupola blu.
    *   `bottom`: Prospettiva dalla parte inferiore dello scafo.
    *   `up/down/left/right/rear`: Cambia la direzione dello sguardo mantenendo la posizione attuale.
*   `enc <algo>`: **Encryption Toggle**. Attiva o disattiva la crittografia in tempo reale. Supporta gli standard **AES-256-GCM**, **ChaCha20**, **ARIA**, **Camellia**, **Blowfish**, **RC4**, **CAST5**, **IDEA**, **3DES** e **PQC (ML-KEM)**. Fondamentale per proteggere le comunicazioni e leggere i messaggi sicuri degli altri capitani.
*   `tor`: **Fire Plasma Torpedo**. Lancia un siluro a guida automatica sul bersaglio agganciato.
*   `tor <H> <M>`: Lancia un siluro in modalità balistica manuale (Heading/Mark).
*   `lock <ID>`: **Target Lock**. Aggancia i sistemi di puntamento sul bersaglio ID (0 per sbloccare). Fondamentale per la guida automatica di Ion Beam e siluri.

### 🆔 Schema Identificativi Galattici (Universal ID)
Per interagire con gli oggetti della galassia tramite i comandi `lock`, `scan`, `pha`, `tor`, `bor` e `dis`, il sistema utilizza un sistema di ID univoci. Usa il comando `srs` per identificare gli ID degli oggetti nel tuo settore.

| Categoria | Intervallo ID | Esempio | Utilizzo Principale |
| :--- | :--- | :--- | :--- |
| **Player** | 1 - 999 | `lock 1` | Tuo vascello o altri giocatori |
| **NPC (Nemici)** | 1.000 - 1.999 | `lock 1050` | Ingaggio tattico (Skirmish AI) |
| **Basi Stellari** | 2.000 - 2.999 | `lock 2005` | Attracco (`doc`) e rifornimento |
| **Pianeti** | 3.000 - 3.999 | `lock 3012` | Estrazione mineraria (`min`) |
| **Stelle** | 4.000 - 6.999 | `lock 4500` | Raccolta energia solare (`sco`) |
| **Buchi Neri** | 7.000 - 7.999 | `lock 7001` | Raccolta antimateria (`har`) |
| **Nebulose** | 8.000 - 8.999 | `lock 8000` | Analisi scientifica e copertura |
| **Pulsar** | 9.000 - 9.999 | `lock 9000` | Monitoraggio radiazioni |
| **Comete** | 10.000 - 10.999| `lock 10001` | Inseguimento e raccolta gas rari |
| **Relitti** | 11.000 - 11.999| `lock 11005` | Abbordaggio (`bor`) e recupero tech |
| **Asteroidi** | 12.000 - 13.999| `lock 12000` | Navigazione di precisione |
| **Mine** | 14.000 - 14.999| `lock 14000` | Allerta tattica ed evitamento |
| **Boe Comm.** | 15.000 - 15.999| `lock 15000` | Link dati e potenziamento `lrs` |
| **Piattaforme** | 16.000 - 16.999| `lock 16000` | Distruzione sentinelle ostili |
| **Rift Spaziali** | 17.000 - 17.999| `lock 17000` | Utilizzo per salti casuali |
| **Mostri** | 18.000 - 18.999| `lock 18000` | Combattimento estremo |
| **Sonde** | 19.000 - 19.999| `scan 19000` | Raccolta dati automatizzata |

**Nota**: Il lock funziona solo se l'oggetto è nel tuo quadrante attuale. Se l'ID esiste ma è lontano, il computer indicherà le coordinate `Q[x,y,z]` del bersaglio.

### 🔄 Workflow Operativo Consigliato
Per eseguire operazioni complesse (estrazione, rifornimento, abbordaggio), segui questa sequenza ottimizzata:

1.  **Identificazione**: Usa `srs` per trovare l'ID dell'oggetto (es. Stella ID `4226`).
2.  **Aggancio**: Esegui `lock 4226`. Vedrai l'ID confermato sul tuo HUD 3D.
3.  **Avvicinamento**: Usa `apr 4226 1.5`. Il pilota automatico ti porterà alla distanza ideale.
4.  **Interazione**: Una volta giunto a destinazione, lancia il comando specifico:
    *   `sco` per le **Stelle** (Ricarica energia).
    *   `min` per i **Pianeti** (Estrazione minerali).
    *   `har` per i **Buchi Neri** (Raccolta antimateria).
    *   `bor` per i **Relitti** (Recupero materiali e riparazioni).
    *   `cha` per le **Comete** (Inseguimento e raccolta gas).
    *   `pha` / `tor` per **Nemici/Mostri/Piattaforme** (Combattimento).

### 📏 Tabella Distanze di Interazione
Distanze espresse in unità di settore (0.0 - 10.0). Se la distanza è superiore al limite, il computer risponderà "No [oggetto] in range".

| Oggetto / Entità | Comando / Azione | Distanza Minima | Effetto / Interazione |
| :--- | :--- | :--- | :--- |
| **Stella** | `sco` | **< 2.0** | Ricarica energia solare (Solar Scooping) |
| **Pianeta** | `min` | **< 2.0** | Estrazione risorse minerarie |
| **Base Stellare** | `doc` | **< 2.0** | Riparazione completa, ricarica energia e siluri |
| **Buco Nero** | `har` | **< 2.0** | Raccolta antimateria (Harvesting) |
| **Relitto** | `dis` | **< 1.5** | Smantellamento per recuperare componenti e risorse |
| **Nave Nemica** | `bor` | **< 1.0** | Operazione di abbordaggio (Boarding Party) |
| **Nave Nemica** | `pha` (Fuoco) | **< 6.0** | Gittata massima efficace dei Ion Beam NPC |
| **Siluro Plasma** | (Impatto) | **< 0.5** | Distanza di collisione per l'esplosione |
| **Boa Com.** | (Passivo) | **< 1.2** | Boost segnale o messaggi automatici |
| **Mostro (Amoeba)** | (Contatto) | **< 1.5** | Inizio drenaggio energetico critico |
| **Crystalline E.** | (Risonanza) | **< 4.0** | Gittata del raggio a risonanza cristallina |
| **Corpi Celesti** | (Collisione) | **< 1.0** | Danno strutturale e attivazione soccorso d'emergenza |

### 🚀 Pilota Automatico (`apr`)
Il comando `apr <ID> <DISTANZA>` permette di avvicinarsi automaticamente a qualsiasi oggetto rilevato dai sensori. Per le entità mobili, l'intercettazione funziona in tutta la galassia.

| Categoria Oggetto | Range ID | Comandi Interazione | Distanza Min. | Note di Navigazione |
| :--- | :--- | :--- | :--- | :--- |
| **Capitani (Giocatori)** | 1 - 32 | `rad`, `pha`, `tor`, `bor` | **< 1.0** (`bor`) | Tracciamento Galattico |
| **Navi NPC (Alieni)** | 1000 - 1999 | `pha`, `tor`, `bor`, `scan` | **< 1.0** (`bor`) | Tracciamento Galattico |
| **Basi Stellari** | 2000 - 2199 | `doc`, `scan` | **< 2.0** | Solo quadrante attuale |
| **Pianeti** | 3000 - 3999 | `min`, `scan` | **< 3.1** | Solo quadrante attuale |
| **Stelle** | 4000 - 6999 | `sco`, `scan` | **< 2.0** | Solo quadrante attuale |
| **Buchi Neri** | 7000 - 7199 | `har`, `scan` | **< 2.0** | Solo quadrante attuale |
| **Nebulose** | 8000 - 8499 | `scan` | - | Solo quadrante attuale |
| **Pulsar** | 9000 - 9199 | `scan` | - | Solo quadrante attuale |
| **Comete** | 10000 - 10299 | `cha`, `scan` | **< 0.6** (Gas) | **Tracciamento Galattico** |
| **Relitti (Derelict)** | 11000 - 11149 | `bor`, `dis`, `scan` | **< 1.5** | Solo quadrante attuale |
| **Asteroidi** | 12000 - 13999 | `min`, `scan` | **< 3.1** | Solo quadrante attuale |
| **Mine** | 14000 - 14999 | `scan` | - | Solo quadrante attuale |
| **Boe di Comm.** | 15000 - 15099 | `scan` | **< 1.2** | Solo quadrante attuale |
| **Piattaforme Difesa** | 16000 - 16199 | `pha`, `tor`, `scan` | - | Solo quadrante attuale |
| **Rift Spaziali** | 17000 - 17049 | `scan` | - | Solo quadrante attuale |
| **Mostri Spaziali** | 18000 - 18029 | `pha`, `tor`, `scan` | **< 1.5** | **Tracciamento Galattico** |

*   `she <F> <R> <T> <B> <L> <RI>`: **Shield Configuration**. Distribuisce energia ai 6 scudi.
*   `clo`: **Cloaking Device**. Attiva/Disattiva occultamento. Consuma 15 unità di energia/tick. Rende invisibili a NPC e altre fazioni, ma è instabile nelle nebulose.
*   `pow <E> <S> <W>`: **Power Distribution**. Ripartisce energia reattore (Motori, Scudi, Armi %). I valori sono relativi (es. `pow 1 1 1` equivale a `pow 33 33 33`).
*   `aux jettison`: **Eject Hyperdrive Synaptics**. Espelle il nucleo di Iperspazio (Manovra suicida).
*   `xxx`: **Self-Destruct**. Autodistruzione sequenziale.

### ⚡ Gestione Reattore e Potenza

Il comando `pow` è fondamentale per la sopravvivenza e la superiorità tattica. Determina come la potenza del reattore principale viene ripartita tra i tre sottosistemi core:

*   **Motori (E)**: Influenza la reattività e la velocità massima dei **Motori a Impulso**. Un'alta allocazione permette manovre rapide e inseguimenti più efficaci.
*   **Scudi (S)**: Governa il **Tasso di Rigenerazione** di tutti i 6 quadranti difensivi. Se gli scudi sono danneggiati, attingeranno energia dal reattore per rigenerarsi.
    *   **Scaling Dinamico**: La velocità di rigenerazione è un prodotto sia della **Potenza (S)** assegnata che dell'**Integrità del Sistema Scudi**. Se il generatore degli scudi è danneggiato, la rigenerazione sarà gravemente ostacolata indipendentemente dall'allocazione di potenza.

#### 🛡️ Meccaniche degli Scudi e Integrità dello Scafo
La nave è protetta da 6 quadranti indipendenti: **Frontale (F), Posteriore (R), Superiore (T), Inferiore (B), Sinistro (L) e Destro (RI)**.

*   **Danno Localizzato**: Gli attacchi (Raggio Ionico/Siluri) colpiscono ora quadranti specifici in base all'angolo relativo di impatto.
*   **Danno Strutturale (Hull Integrity)**: Rappresenta la salute fisica della nave (0-100%). Se un quadrante di scudi raggiunge lo 0% o il colpo è eccessivamente potente, il danno residuo colpisce direttamente l'integrità strutturale.
*   **Danno ai Sistemi Interni**: Quando lo scafo viene colpito direttamente (scudi a zero), c'è un'alta probabilità di subire danni ai sottosistemi (motori, armi, sensori, ecc.).
    *   **Raggio Ionico**: Probabilità moderata di danno casuale.
    *   **Siluri**: Probabilità molto alta (>50%) di guasti critici ai sistemi interni.
*   **Hull Plating (Composite)**: La placcatura aggiuntiva (comando `hull`) funge da buffer: assorbe il danno fisico *prima* che questo intacchi la Hull Integrity.
*   **Condizione di Distruzione**: Se la **Hull Integrity raggiunge lo 0%**, la nave esplode istantaneamente, indipendentemente dai livelli di energia o scudi rimanenti.
*   **Rigenerazione Continua**: A differenza dei vecchi sistemi, la rigenerazione degli scudi è continua ma scala con la salute dell'hardware.
*   **Collasso degli Scudi**: Se un quadrante raggiunge lo 0% di integrità, i colpi successivi provenienti da quella direzione infliggeranno danni diretti allo scafo e al reattore energetico principale.

#### 🛸 Dispositivo di Occultamento (Cloaking Device)
Il comando `clo` attiva una tecnologia di occultamento avanzata che manipola la luce e i sensori per rendere il vascello invisibile.

*   **Invisibilità Tattica**: Una volta attivo, non sarai rilevabile dai sensori (`srs`/`lrs`) degli altri giocatori (a meno che non appartengano alla tua stessa fazione). Le navi NPC ti ignoreranno completamente e non inizieranno manovre di attacco.
*   **Costi Energetici**: Il mantenimento del campo di occultamento è estremamente energivoro, consumando **15 unità di energia per tick** logico. Monitora attentamente le riserve del reattore.
*   **Limitazioni Sensori**: Durante l'occultamento, i sensori di bordo subiscono interferenze ("Sensors limited"), rendendo più difficile l'acquisizione di dati ambientali precisi.
*   **Instabilità nelle Nebulose**: All'interno delle nebulose, il campo di occultamento diventa instabile a causa dei gas ionizzati. Questo può causare fluttuazioni nel drenaggio energetico e inibire la rigenerazione degli scudi.
*   **Feedback Visivo**: Quando occultato, la nave originale scompare e viene sostituita da un modello a reticolo (wireframe) con un **effetto "Blue Glowing"** pulsante. L'HUD mostrerà lo stato `[ CLOAKED ]` in magenta.
*   **Restrizioni di Combattimento**: Non è possibile fare fuoco con i **Raggio Ionico** (`pha`) o lanciare **Siluri** (`tor`) mentre il dispositivo di occultamento è attivo. È necessario disattivarlo per ingaggiare il nemico.
*   **Strategia NPC (Xylarii)**: L'Impero Stellare Xylario utilizza tattiche di occultamento avanzate; le loro navi rimarranno occultate durante il pattugliamento o la fuga, rivelandosi solo per sferrare un attacco.

*   **Armi (W)**: Scala direttamente l'**Intensità dei Raggio Ionico** e il **Tasso di Ricarica**. Un'allocazione elevata convoglia più energia nei banchi Raggio Ionico, permettendo di infliggere danni esponenzialmente maggiori e ricaricare il condensatore molto più velocemente.

#### 🎯 Nota Tactical Ordnance: Raggio Ionico e Siluri
*   **Condensatore Raggio Ionico**: Visibile nell'HUD come "Ion Beam CAPACITOR: XX%". Rappresenta l'energia attualmente accumulata per il fuoco.
    *   **Fuoco**: Ogni colpo consuma una parte del condensatore. Se la carica è inferiore al 10%, i Raggio Ionico non possono fare fuoco.
    *   **Ricarica**: Si ricarica automaticamente ogni secondo. La velocità di ricarica è potenziata assegnando più potenza alle **Armi (W)** tramite il comando `pow`.
*   **Integrità Raggio Ionico**: Visibile nell'HUD come "Ion Beam INTEGRITY: XX%". Rappresenta lo stato dell'hardware. Il danno inflitto è moltiplicato per questo valore. Usa `rep 4` per ripararli.
*   **Tubi di Lancio**: Visibili nell'HUD come "TUBES: <STATO>".
    *   **READY**: Il sistema è armato e pronto al fuoco.
    *   **FIRING...**: Un siluro è attualmente in volo. Nuovi lanci sono inibiti fino all'impatto o all'uscita dal settore.
    *   **LOADING...**: Sequenza di raffreddamento e ricarica post-lancio (circa 5 secondi).
    *   **OFFLINE**: L'integrità hardware è inferiore al 50%. Il lancio è impossibile fino alla riparazione (`rep 5`).

### 💓 Supporto Vitale e Sicurezza Equipaggio
L'HUD mostra "LIFE SUPPORT: XX.X%", valore direttamente collegato all'integrità dei sistemi vitali della nave.
*   **Inizializzazione**: Ogni missione inizia con il Supporto Vitale al 100%.
*   **Soglia Critica**: Se la percentuale scende sotto il **75%**, l'equipaggio inizierà a subire perdite periodiche a causa di guasti ambientali (radiazioni, mancanza di ossigeno o fluttuazioni gravitazionali).
*   **Riparazioni d'Emergenza**: Mantenere il Supporto Vitale sopra la soglia è la massima priorità. Usa immediatamente `rep 7` se l'integrità è compromessa.
*   **Fallimento Missione**: Se il numero di membri dell'equipaggio raggiunge lo **zero**, il vascello è dichiarato perso e la simulazione termina.

**Feedback HUD**: L'allocazione attuale è visibile nel pannello diagnostico in basso a destra come `POWER: E:XX% S:XX% W:XX%`. Monitorare questo valore è essenziale per ottimizzare la nave in base alla fase della missione (Esplorazione vs Combattimento).

### 📦 Operazioni e Risorse
*   `bor [ID]`: **Boarding Party**. Invia squadre di arrembaggio (Dist < 1.0).
    *   Funziona sul **bersaglio attualmente agganciato** se l'ID non è specificato.
    *   **Interazione NPC/Relitti**: Premi automatici (Aetherium, Chip, Riparazioni, Superstiti o Prigionieri).
    *   **Interazione tra Giocatori**: Apre un **Menu Tattico Interattivo** con scelte specifiche:
        *   **Vascelli Alleati**: `1`: Trasferimento Energia, `2`: Supporto Tecnico (Riparazioni), `3`: Rinforzi Equipaggio.
        *   **Vascelli Ostili**: `1`: Sabotaggio Sistemi, `2`: Raid della Stiva (Furto Risorse), `3`: Cattura Ostaggi.
    *   **Selezione**: Rispondi con il numero `1`, `2`, o `3` per eseguire l'azione scelta.
    *   **Rischi**: Probabilità di resistenza (30% per i giocatori, variabile per NPC) con possibili perdite tra i membri del team.
*   `dis`: **Dismantle**. Smantella relitti nemici per risorse (Dist < 1.5).
*   `min`: **Mining**. Estrae risorse da un pianeta o asteroide in orbita (Dist < 3.1).
    *   **Priorità Selettiva**:
        1.  Se un bersaglio è agganciato (`lock <ID>`), il sistema darà la precedenza assoluta a quello.
        2.  In assenza di lock, verrà estratto il minerale dall'oggetto **più vicino** in assoluto.
    *   **Feedback Radio**:
        *   Asteroidi: `[RADIO] MINING (Alliance Command): Asteroid extraction complete.`
        *   Pianeti: `[RADIO] GEOLOGY (Alliance Command): Planetary mining successful.`
*   `sco`: **Solar Scooping**. Raccoglie energia da una stella (Dist < 2.0).
*   `har`: **Harvest Plasma Reserves**. Raccoglie antimateria da un buco nero (Dist < 2.0).
*   `con T A`: **Convert Resources**. Converte materie prime in energia o siluri (`T`: tipo risorsa, `A`: quantità).
    *   `1`: Aetherium -> Energia (x10).
    *   `2`: Neo-Titanium -> Energia (x2).
    *   `3`: Void-Essence -> Siluri (1 ogni 20).
    *   `6`: Gas -> Energia (x5).
    *   `7`: Composite -> Energia (x4).
    *   `8`: **Dark-Matter** -> Energia (x25). [Efficienza Massima]. Minerale radioattivo rarissimo. Convertilo con `con 8 <quantità>` per ricaricare istantaneamente le riserve energetiche del Cargo.
*   `load <T> <A>`: **Load Systems**. Trasferisce energia o siluri dalla stiva ai sistemi attivi.
    *   `1`: Energia (Reattore Principale). Capacità max: 9.999.999 unità. Consente di convertire l'Plasma Reserves raccolta dai Buchi Neri in energia operativa.
    *   `2`: Siluri (Tubi di Lancio). Capacità max: 1000 unità.

#### 🏗️ Rinforzo Scafo (Hull Plating)
*   `hull`: **Reinforce Hull**. Utilizza **100 unità di Composite** per applicare una placcatura rinforzata allo scafo (+500 unità di integrità). 
    *   La placcatura in Composite funge da scudo fisico secondario, assorbendo il danno residuo che supera gli scudi energetici prima che questo colpisca il reattore principale.
    *   Lo stato della placcatura è visibile nell'HUD 3D e tramite il comando `sta`.
*   `inv`: **Inventory**. Mostra il contenuto della stiva (Cargo Bay), inclusi materiali grezzi (**Graphene**, **Synaptics**, **Composite**) e **Prigionieri**.

### 📦 Gestione Carico e Risorse

Space GL distingue tra **Sistemi Attivi**, **Stoccaggio Stiva** e **Unità di Detenzione (Prison Unit)**. Questa distinzione è visibile nell'HUD come `ENERGY: X (CARGO: Y)`.



*   **Energia/Siluri Attivi**: Sono le risorse immediatamente utilizzabili dai sistemi della nave.

*   **Riserve in Stiva (Cargo Bay)**: Risorse conservate per il rifornimento a lungo raggio.

        *   **Tabella Risorse**:

            1. **Aetherium**: Salto Hyperdrive e conversione energetica.

            2. **Neo-Titanium**: Riparazione scafi e conversione energia.

            3. **Void-Essence**: Materiale per testate Siluri al plasma (`[WARHEADS]`).

            4. **Graphene**: Lega strutturale avanzata.

            5. **Synaptics**: Chip per riparazioni sistemi complessi.

            6. **Nebular Gas**: Raccolti dalle comete, conversione energia.

            7. **Composite**: Placcatura corazzata scafo.

            8. **Dark-Matter**: Minerale raro e radioattivo trovato in asteroidi e pianeti speciali. Utilizzato per tecnologie sperimentali e potenziamento sistemi.

*   **Prison Unit**: Unità dedicata alla detenzione del personale nemico catturato durante gli arrembaggi. È monitorata in tempo reale nell'HUD vitale accanto all'equipaggio.

*   **Conversione Risorse**: Materie prime devono essere convertite (`con`) in **CARGO Plasma Reserves** o **CARGO Torpedoes** prima di essere caricate nei sistemi attivi.

*   `rep [ID]`: **Repair**. Ripara un sistema danneggiato (salute < 100%) riportandolo alla piena efficienza. Fondamentale per eliminare il rumore telemetrico dei sensori o riattivare armi offline.
    *   **Costo**: Ogni riparazione consuma **50 Neo-Titanium** e **10 Synaptics**.
    *   **Funzionamento**: Se usato senza ID, elenca tutti i 10 sistemi con il loro stato di integrità.
    *   **ID Sistemi**: `0`: Hyperdrive, `1`: Impulse, `2`: Sensori, `3`: Trasportatori, `4`: Raggio Ionico, `5`: Siluri, `6`: Computer, `7`: Supporto Vitale, `8`: Scudi, `9`: Ausiliari.
*   **Gestione Equipaggio**: 
    *   Il numero iniziale di personale dipende dalla classe della nave (es. 1012 per la Explorer, 50 per la Escort).
    *   **Integrità Vitale**: Se il sistema di **Supporto Vitale** (`Life Support`) scende sotto il 75%, l'equipaggio inizierà a subire perdite periodiche.
    *   **Integrità Scudi**: Se l'integrità del **Sistema Scudi (ID 8)** è bassa, la ricarica automatica dei 6 quadranti è rallentata.
    *   **Condizione di Fallimento**: Se l'equipaggio raggiunge quota **zero**, la missione termina e la nave è considerata perduta.

### 🛡️ Crittografia e Guerra Elettronica (Deep Space Security)
Il sistema implementa un layer di sicurezza di grado militare per tutte le comunicazioni tra i vascelli e il comando di flotta.

*   **Implementazione Tecnica**: La cifratura è gestita tramite la libreria **OpenSSL**, utilizzando algoritmi a 256-bit con autenticazione del pacchetto (GCM/Poly).
*   **Firma Digitale (Ed25519)**: Oltre alla cifratura, ogni messaggio radio include una firma crittografica generata tramite curve ellittiche (**Ed25519**). Questo garantisce **Autenticità** (solo il vero capitano può firmare) e **Integrità** (il messaggio non è stato alterato).
    *   **Verificato**: I messaggi con una firma valida appaiono con il tag verde **`[VERIFIED]`**.
    *   **Allerta Manomissione**: Se una firma è presente ma non valida, appare il tag rosso **`[UNVERIFIED]`**, avvertendo di potenziali intercettazioni subspaziali o corruzione dei dati.
*   **Rotating Frequency Codes**: Ogni messaggio ha una firma unica basata sul `frame_id` del server. Questo impedisce a un nemico di "registrare e riprodurre" i tuoi comandi.
*   **Session Persistence & Handshake Safety**: Sebbene il server salvi il profilo del capitano, i protocolli di crittografia vengono resettati a `off` a ogni nuova connessione. Questo garantisce che i messaggi di benvenuto e la sincronizzazione iniziale siano sempre leggibili, evitando "lock-out" dovuti a protocolli dimenticati.
*   **Dynamic Diagnostic Feedback**: In caso di discrepanza tra i protocolli dei capitani (es. uno usa AES e l'altro ChaCha), il computer di bordo analizza il rumore binario e fornisce suggerimenti contestuali (`[HINT]`) per aiutare il capitano a sincronizzare la frequenza.
*   **Decrittazione Selettiva (Radio Frequencies)**: I diversi algoritmi agiscono come frequenze di comunicazione separate.
    *   Se un capitano trasmette in `enc aes`, solo chi è sintonizzato su `enc aes` potrà leggere.
    *   Se sei sintonizzato su un protocollo diverso (es. ascolti in AES ma ricevi ChaCha), vedrai il messaggio **<< SIGNAL DISTURBED: FREQUENCY MISMATCH >>**.
    *   Questo obbliga gli alleati a coordinarsi preventivamente sulla "frequenza" di missione da utilizzare.
*   **Sincronizzazione Deterministica**: Ogni pacchetto trasporta il proprio `origin_frame`, permettendo al ricevente di ricostruire l'esatta frequenza di cifratura originale, rendendo il sistema immune ai ritardi di rete (lag).
*   **Comandi Operativi**:
    *   `enc aes`: Attiva lo standard **AES-256-GCM** (Robusto, accelerato hardware).
    *   `enc chacha`: Attiva lo standard **ChaCha20-Poly1305** (Moderno, ultra-veloce).
    *   `enc aria`: Attiva lo standard **ARIA-256-GCM** (Standard Sudcoreano).
    *   `enc camellia`: Attiva **Camellia-256-CTR** (Standard Xylario ad alta sicurezza).
    *   `enc bf`: Attiva **Blowfish-CBC** (Protocollo commerciale Gilded).
    *   `enc rc4`: Attiva **RC4 Stream** (Link tattico a bassa latenza).
    *   `enc cast`: Attiva **CAST5-CBC** (Standard della Vecchia Repubblica).
    *   `enc idea`: Attiva **IDEA-CBC** (Protocollo Maquis/Resistenza).
    *   `enc 3des`: Attiva **Triple DES-CBC** (Standard antico delle sonde terrestri).
    *   `enc pqc`: Attiva **ML-KEM-1024** (Crittografia Post-Quantistica). Standardizzato dal NIST per resistere agli attacchi dei futuri computer quantistici.
    *   `enc off`: Disattiva i protocolli di sicurezza (Comunicazione RAW).### 📡 Comunicazioni e Varie
*   `rad <MSG>`: Invia messaggio radio a tutti (Canale aperto).
    *   **Tabella Fazioni (@Fac)**:
        | Fazione | Esteso | Abbreviato |
        | :--- | :--- | :--- |
        | **Alleanza** | `@Alliance` | `@Fed` |
        | **Korthian** | `@Korthian` | `@Kli` |
        | **Xylarii** | `@Xylari` | `@Rom` |
        | **Swarm** | `@Swarm` | `@Bor` |
        | **Vesperiani** | `@Vesperian` | `@Car` |
        | **Dominio** | `@Ascendant` | `@Jem` |
        | **Quarzitei** | `@Quarzite` | `@Tho` |
        | **Gilded** | `@Gilded` | `@Fer` |
        | **Specie 8472** | `@FluidicVoid`| `@8472` |
        | **Saurian / Cryos / Apex** | Nome Esteso | - |
*   `rad #ID <MSG>`: Messaggio privato al giocatore ID.
*   `psy`: **Psychological Warfare**. Tenta bluff (Manovra Corbomite).
*   `axs` / `grd`: Attiva/Disattiva guide visive 3D (Assi / Griglia).
Il visualizzatore 3D non è una semplice finestra grafica, ma un'estensione del ponte di comando che fornisce dati telemetrici sovrapposti (Augmented Reality) per supportare il processo decisionale del capitano.

#### 🎯 Proiezione Tattica Integrata
Il sistema utilizza algoritmi di proiezione spaziale per ancorare le informazioni direttamente sopra le entità rilevate:
*   **Targeting Tags**: Ogni vascello viene identificato con un'etichetta dinamica avanzata.
    *   **Giocatori Alleanza**: Mostra `Alliance - [Classe] ([Nome Capitano])`.
    *   **Giocatori Alieni**: Mostra `[Fazione] ([Nome Capitano])`.
    *   **Unità NPC**: Mostra `[Fazione] [ID]`.
*   **Health Bars**: Indicatori cromatici della salute (Verde/Giallo/Rosso) visualizzati sopra ogni nave e stazione, permettendo di valutare istantaneamente lo stato del nemico senza consultare i log testuali.
*   **Visual Latching**: Gli effetti di combattimento (Ion Beam, esplosioni) sono sincronizzati temporalmente con la logica del server, fornendo un feedback visivo immediato all'impatto dei colpi.

#### 🧭 Bussola Tattica 3D (`axs`)
Attivando gli assi visivi (`axs`), il simulatore proietta un sistema di riferimento sferico centrato sulla propria nave. 

**Nota sulla Convenzione degli Assi**: Il motore grafico utilizza lo standard **OpenGL (Y-Up)**.
*   **X (Rosso)**: Asse trasversale (Sinistra/Destra).
*   **Y (Verde)**: Asse verticale (Alto/Basso). È l'asse di rotazione per il *Heading*.
*   **Z (Blu)**: Asse longitudinale (Profondità/Avanzamento).
*   **Coordinate di Confine**: Al centro di ogni faccia del cubo tattico vengono proiettate le coordinate `[X,Y,Z]` dei quadranti adiacenti.

---

### 🖥️ Architettura del Server e Telemetria GDIS

All'avvio del `stellar_server`, il sistema esegue una diagnostica completa dell'infrastruttura ospite, visualizzando un pannello di telemetria **GDIS (Library Computer Access and Retrieval System)**.

#### 📊 Dati Monitorati
*   **Infrastruttura Logica**: Identificazione dell'host, versione del kernel Linux e delle librerie core (**GNU libc**).
*   **Allocazione Memoria**: 
    *   **Physical RAM**: Stato della memoria fisica disponibile.
    *   **Shared Segments (SHM)**: Monitoraggio dei segmenti di memoria condivisa attivi, vitali per il *Direct Bridge* IPC.
*   **Topologia di Rete**: Elenco delle interfacce attive, indirizzi IP e statistiche di traffico (**RX/TX**) in tempo reale recuperate dal kernel.
*   **Dinamica del Sottospazio**: Carico medio del sistema (*Load Average*) e numero di task logici attivi.

Questa diagnostica assicura che il server operi in un ambiente nominale prima di aprire i canali di comunicazione Dello spazio profondo sulla porta `3073`.


#### 📟 Telemetria e Monitoraggio HUD
L'interfaccia a schermo (Overlay) fornisce un monitoraggio costante dei parametri vitali:
*   **Reactor & Shield Status**: Visualizzazione in tempo reale dell'energia disponibile e della potenza media della griglia difensiva.
*   **Cargo Monitoring**: Monitoraggio esplicito delle riserve di **CARGO ANTIMATTER** e **CARGO TORPEDOES** per il rifornimento rapido.
*   **Hull Integrity**: Stato fisico dello scafo (0-100%). Se scende a zero, il vascello è perduto.
*   **Hull Plating**: Indicatore in oro dell'integrità dello scafo rinforzato con Composite (visibile solo se presente).
*   **Coordinate di Settore**: Conversione istantanea dei dati spaziali in coordinate relative `[S1, S2, S3]` (0.0 - 10.0), speculari a quelle utilizzate nei comandi `nav` e `imp`.
*   **Rilevatore di Minacce**: Un contatore dinamico indica il numero di vascelli ostili rilevati dai sensori nel quadrante attuale.
*   **Suite di Diagnostica del Sottospazio (Deep Space Uplink)**: Un pannello diagnostico avanzato (basso a destra) che monitora la salute del collegamento neurale/dati. Mostra in tempo reale l'uptime del link, il **Pulse Jitter**, l'**Integrità del Segnale**, l'efficienza del protocollo e lo stato attivo della crittografia **AES-256-GCM**.

#### 🛠️ Personalizzazione della Vista
Il comandante può configurare la propria interfaccia tramite comandi CLI rapidi:
*   `grd`: Attiva/Disattiva la **Griglia Tattica Galattica**, utile per percepire profondità e distanze.
*   `axs`: Attiva/Disattiva la **Bussola Tattica AR**. Questa bussola olografica è ancorata alla posizione della nave: l'anello di Azimut (Heading) rimane orientato verso il Nord galattico (coordinate fisse), mentre l'arco di Elevazione (Mark) ruota solidalmente con il vascello, fornendo un riferimento di navigazione immediato per le manovre di combattimento.
*   `h` (tasto rapido): Nasconde completamente l'HUD per una visione "cinematica" del settore.
*   **Zoom & Rotazione**: Controllo totale della telecamera tattica tramite mouse o tasti `W/S` e frecce direzionali.

---

## ⚠️ Rapporto Tattico: Minacce e Ostacoli

### Capacità delle Navi NPC
Le navi controllate dal computer (Korthian, Xylarii, Swarm, ecc.) operano con protocolli di combattimento standardizzati:
*   **Armamento Primario**: Attualmente, le navi NPC sono equipaggiate esclusivamente con **Banchi Ion Beam**.
*   **Potenza di Fuoco**: I Ion Beam nemici infliggono un danno costante di **10 unità** di energia per colpo (ridotto per bilanciamento).
*   **Portata d'Ingaggio**: Le navi ostili apriranno il fuoco automaticamente se un giocatore entra nel raggio di **6.0 unità** (Settore).
*   **Cadenza di Tiro**: Circa un colpo ogni 5 secondi.
*   **Tattica**: Le navi NPC non utilizzano Siluri al plasma. La loro strategia principale consiste nell'avvicinamento diretto (Chase) o nella fuga se l'energia scende sotto livelli critici.

### ☄️ Dinamica dei Siluri al plasma
I siluri (comando `tor`) sono armi a simulazione fisica con precisione millimetrica:
*   **Collisione**: I siluri devono colpire fisicamente il bersaglio (distanza **< 0.8**) per esplodere (raggio aumentato per evitare il tunneling).
*   **Guida**: Se lanciati con un `lock` attivo, i siluri correggono la rotta del **35%** per ogni tick verso il bersaglio, permettendo di colpire navi agili.
*   **Inseguimento Comete**: È possibile usare il comando `cha` (Chase) per seguire le comete lungo la loro orbita galattica, facilitando la raccolta di gas.
*   **Ostacoli**: Corpi celesti come **Stelle, Pianeti e Buchi Neri** sono oggetti fisici solidi. Un siluro che impatta contro di essi verrà assorbito/distrutto senza colpire il bersaglio dietro di essi. Sfruttate il terreno galattico per coprirvi!
*   **Basi Stellari**: Anche le basi stellari bloccano i siluri. Attenzione al fuoco amico o incidentale.

### 🌪️ Anomalie Spaziali e Pericoli Ambientali
Il quadrante è disseminato di fenomeni naturali rilevabili sia dai sensori che dalla **visuale tattica 3D**:
*   **Nebulose (ID 8xxx)**:
    *   **Classi**: Standard, High-Energy, Dark Matter, Ionic, Gravimetric, Temporal.
    *   **Effetto**: Nubi di gas e particelle che interferiscono con i sensori a corto e lungo raggio (rumore telemetrico e distorsione).
    *   **Visuale 3D**: Volumi di gas colorati in base alla classe (Viola/Azzurro per Standard, Giallo/Arancio per High-Energy, Nero/Viola per Dark Matter, ecc.).
    *   **Pericolo**: Stazionare all'interno (Distanza < 2.0) causa un costante drenaggio di energia e inibisce la rigenerazione degli scudi.
    *   **Vantaggio**: Forniscono copertura tattica naturale (occultamento passivo) contro i sensori nemici.
*   **Pulsar (ID 5xxx)**:
    *   **Effetto**: Stelle di neutroni in rapida rotazione che emettono radiazioni letali.
    *   **Visuale 3D**: Visibili come nuclei luminosi con fasci di radiazioni rotanti.
    *   **Pericolo**: Avvicinarsi troppo (Distanza < 2.5) danneggia gravemente gli scudi e uccide rapidamente l'equipaggio per irraggiamento.
*   **Comete (ID 6xxx)**:
    *   **Effetto**: Oggetti mobili veloci che attraversano il settore.
    *   **Visuale 3D**: Nuclei ghiacciati con scia azzurra di gas e polveri.
    *   **Risorsa**: Avvicinarsi alla coda (< 0.6) permette la raccolta di gas rari per la stiva.
*   **Campi di Asteroidi (ID 8xxx)**:
    *   **Effetto**: Ammassi di rocce spaziali di varie dimensioni.
    *   **Visuale 3D**: Rocce rotanti marroni di forma irregolare.
    *   **Pericolo**: Navigare all'interno a velocità d'impulso elevata (> 0.1) causa danni continui a scudi e motori.
*   **Relitti alla Deriva (ID 7xxx)**:
    *   **Effetto**: Navi abbandonate della Alleanza o di altre fazioni.
    *   **Visuale 3D**: Scafi scuri e freddi che oscillano lentamente nello spazio.
    *   **Opportunità**: Possono essere esplorati tramite il comando `bor` (arrembaggio) per recuperare Dilithio, Chip Synapticsi o per eseguire riparazioni d'emergenza istantanee.
*   **Campi Minati (ID 9xxx)**:
    *   **Effetto**: Zone difensive con mine occulte piazzate da fazioni ostili.
    *   **Visuale 3D**: Piccole sfere metalliche con aculei e luce rossa pulsante (visibili solo a distanza < 1.5).
    *   **Pericolo**: La detonazione causa danni massicci a scudi ed energia. È consigliabile l'uso del comando `scan` per rilevarle prima dell'ingresso.
*   **Boe di Comunicazione (ID 10xxx)**:
    *   **Effetto**: Nodi di rete della Alleanza per il monitoraggio del settore.
    *   **Visuale 3D**: Strutture a traliccio con antenne rotanti e segnale blu pulsante.
    *   **Vantaggio**: Trovarsi in prossimità di una boa (< 1.2) potenzia i sensori a lungo raggio (`lrs`), rivelando l'esatta composizione dei quadranti adiacenti invece di un semplice conteggio.
*   **Piattaforme Difensive (ID 11xxx)**:
    *   **Effetto**: Sentinelle statiche pesantemente armate a protezione di zone strategiche.
    *   **Visuale 3D**: Strutture esagonali metalliche con banchi Ion Beam attivi e nucleo energetico.
    *   **Pericolo**: Estremamente pericolose se approcciate senza scudi al massimo. Aprono il fuoco automaticamente contro bersagli non occultati entro un raggio di 5.0 unità.
*   **Faglie Spaziali (ID 12xxx)**:
    *   **Effetto**: Teletrasporti naturali instabili causati da strappi nel tessuto spaziotemporale che ignorano le normali leggi della navigazione Hyperdrive.
    *   **Visuale 3D**: Appaiono come anelli di energia ciano rotanti. Sulla mappa galattica e nei sensori (`srs` o `lrs`) sono segnate con il colore **Ciano** o la lettera **R**.
    *   **Rischio/Opportunità**: Entrare in una faglia (Distanza < 0.5) proietta istantaneamente la nave in un punto completamente casuale dell'universo (quadrante e settore aleatori). Può essere un pericolo fatale (es. territorio Swarm) o l'unica via di fuga veloce durante un attacco critico.
*   **Mostri Spaziali (ID 13xxx)**:
    *   **Entità Cristallina**: Predatore geometrico che insegue le navi e spara raggi a risonanza cristallina.
    *   **Ameba Spaziale**: Gigantesca forma di vita che drena energia per contatto.
    *   **Pericolo**: Estremamente rari e pericolosi. Richiedono tattiche di squadra o massima potenza di fuoco.
*   **Tempeste Ioniche**:
    *   **Effetto**: Eventi casuali globali sincronizzati in tempo reale sulla mappa.
    *   **Frequenza**: Elevata (media statistica di un evento ogni 5-6 minuti).
    *   **Impatto Tecnico**: Colpire una tempesta **dimezza istantaneamente** la salute dei sensori (ID 2).
    *   **Degrado Funzionale**: Sensori danneggiati (< 100%) producono "rumore" nei rapporti SRS/LRS (oggetti fantasma, dati mancanti o coordinate imprecise). Sotto il 25%, i sensori diventano quasi inutilizzabili.
    *   **Dettagli Tecnici**: Controllo ogni 1000 tick (33s), probabilità evento 20%, di cui il 50% sono tempeste ioniche.
    *   **Pericolo**: Possono accecare i sensori o spostare violentemente la nave fuori dalla rotta stabilita.

## 📡 Dinamiche di Gioco ed Eventi
L'universo di Space GL è animato da una serie di eventi dinamici che richiedono decisioni rapide da parte del comando.

#### ⚡ Eventi Casuali del Settore
*   **Sbalzi del Sottospazio (Deep Space Surges)**: Fluttuazioni improvvise che possono ricaricare parzialmente le riserve energetiche o causare un sovraccarico con conseguente perdita di energia.
*   **Cesoie Spaziali (Spatial Shear)**: Violente correnti gravitazionali che colpiscono la nave durante la navigazione, spingendola fisicamente fuori rotta.
*   **Emergenza Supporto Vitale**: Se il sistema `Life Support` è danneggiato, l'equipaggio inizierà a subire perdite. È una condizione critica che richiede riparazioni d'urgenza o l'attracco a una base stellare.

#### 🚨 Protocolli Tattici e di Emergenza
*   **Bluff Corbomite**: Il comando `psy` permette di trasmettere un segnale di minaccia nucleare fittizio. Se il bluff ha successo, i vascelli nemici entreranno in modalità di ritirata immediata.
*   **Protocollo Soccorso (Emergency Rescue)**: In caso di distruzione del vascello o collisione fatale, al successivo rientro il Comando della Flotta attiverà una missione di soccorso automatica, riposizionando la nave in un settore sicuro e ripristinando i sistemi core all'80%.
*   **Resistenza all'Abbordaggio**: Le operazioni di arrembaggio (`bor`) non sono prive di rischi; le squadre possono essere respinte, causando danni interni ai circuiti della propria nave.

---

## 🎖️ Registro Storico dei Grandi Comandanti (Database GDIS)

Il database centrale GDIS conserva le imprese dei comandanti che hanno plasmato i confini dello spazio conosciuto attraverso le tenebre e la luce.

#### 🌌 1. Alleanza Stellare (Alliance)
*   🛡️ **Ammiraglio Hyperion Niklaus**: Conosciuto come "Il Muro di Orione", guidò la difesa della Aegis durante la prima grande invasione Swarm.
*   ⚓ **Capitano Lyra Vance**: La leggendaria esploratrice che mappò l'Einstein-Rosen Bridge verso il quadrante Delta con una nave di classe Scout.
*   📜 **Comandante Leandros Thorne**: Fine diplomatico e tattico, celebre per il Trattato di Aetherium che pose fine alla guerra secolare con i Korthian.

#### ⚔️ 2. Impero Korthian
*   🩸 **Signore della Guerra Khorak**: Il più brutale tattico dell'impero, famoso per la sua dottrina del "Fuoco Perpetuo" e la conquista del Settore Nero.
*   🗡️ **Generale Valkar**: Comandante leggendario che unificò le casate in lotta sotto un unico vessillo di conquista galattica.

#### 🎭 3. Impero Stellare Xylari
*   🐍 **Alto Praetor Nyx**: Maestro dell'occultamento e del sabotaggio, sparì per dieci anni prima di riemergere con una flotta fantasma nel cuore del territorio nemico.
*   👁️ **Inquisitore Malakor**: Il primo a utilizzare le frequenze di crittografia Camellia per manipolare i flussi di dati dei sensori nemici.

#### 🕸️ 4. Collettivo Swarm
*   🤖 **Unità Nexus-Alpha**: La prima intelligenza alveare ad aver coordinato l'assimilazione tecnologica di interi sistemi stellari.
*   🔗 **Sinergia Prime**: Un'entità biomeccanica incaricata di ottimizzare il consumo di massa stellare nelle nebulose energetiche.

#### 🏛️ 5. Unione Vesperiana
*   📐 **Legato Thrax**: Architetto della difesa galattica, noto per aver trasformato semplici asteroidi in fortezze inespugnabili.

#### 🔮 6. L'Ascendenza (The Ascendancy)
*   🛐 **Primo Archonte Voth**: La guida spirituale e militare che portò la sua flotta di "Ascendenti" attraverso il Grande Vuoto.

#### 💎 7. Matrice Quarzite
*   💠 **Rifrazione Zero**: Un'entità cristallina pura capace di calcolare rotte iperspaziali a una velocità superiore a qualsiasi computer biologico.

#### 💰 8. Cartello Dorato (Gilded Cartel)
*   📈 **Barone Silas**: Il magnate che monopolizzò il commercio di Void-Essence e Aetherium in tre quadranti.

#### ❄️ 9. Enclave Cryos
*   🧊 **Custode Boreas**: Governatore delle distese gelide, esperto in tattiche di guerriglia termica e soppressione di segnale.

#### 🎯 10. Apex Stalkers
*   🏹 **Cacciatore Kael**: Conosciuto come "Il Fantasma del Settore Zero", rinomato per non aver mai mancato un bersaglio con i suoi siluri plasma a guida manuale.

---


### 🔵 Profili Operativi per Classe di Vascello (Alleanza)

In Space GL, la scelta della classe di vascello definisce il profilo operativo del Comandante. Di seguito, i riferimenti per le classi principali:

#### 🏛️ Classe Legacy (Incrociatore Pesante)
Il simbolo dell'esplorazione dell'Alleanza. Vascello bilanciato, versatile e robusto.
*   **Comandante di Riferimento**: **Hyperion Niklaus**. La sua leadership sulla Aegis originale definì gli standard tattici dell'accademia.

#### 🛡️ Classe Explorer (Nave Ammiraglia)
Progettata per missioni di lunga durata e primo contatto. Dispone dei sistemi GDIS più avanzati.
*   **Comandante di Riferimento**: **Lyra Vance**. Eccelleva nell'uso dei sensori a lungo raggio per evitare scontri non necessari.

#### ⚔️ Classe Flagship (Incrociatore Tattico)
La massima espressione della potenza di fuoco dell'Alleanza, equipaggiata con banchi Ion Beam pesanti.
*   **Comandante di Riferimento**: **Leandros Thorne**. Famoso per l'uso coordinato di scudi localizzati e raffiche di siluri plasma.

#### 🔭 Classe Science Vessel (Esploratore Scientifico)
Vascello specializzato nell'analisi di anomalie spaziali e raccolta di Aetherium.
*   **Comandante di Riferimento**: **Inquisitore Malakor** (Acquisito). Sebbene Xylari, le sue teorie sulla risonanza spaziale sono studiate in ogni missione scientifica.

#### 🛠️ Altre Classi Operative
L'Alleanza impiega inoltre vascelli specializzati come la classe **Carrier** (coordinamento droni) e **Tactical Cruiser** (difesa perimetrale), ciascuno ottimizzato per scenari di crisi specifici.

---

### 🛰️ Nomenclatura Navale Alliance (Standard GDIS)

Per facilitare il coordinamento tattico, il sistema GDIS adotta una nomenclatura standardizzata per i componenti dei vascelli dell'Alleanza, qui illustrata sulla configurazione "Monoblocco" della classe Legacy:

1.  **Primary Hull (Command Module)**: Il corpo discoidale primario, sede dei ponti di comando, alloggi e laboratori scientifici.
2.  **Tactical Hub (Bridge Module)**: La cupola rinforzata superiore, centro di calcolo per il puntamento delle armi e la gestione della flotta.
3.  **Engineering Section (Secondary Hull)**: La sezione oblunga posteriore integrata, progettata per ospitare il reattore a plasma e i serbatoi di Aetherium.
4.  **Main Deflector Array (Rear Sphere)**: Una sfera risonante distanziata dal corpo principale, utilizzata per la deflessione di particelle e la stabilizzazione dei flussi Hyperdrive.
5.  **Energy Resonance Rings**: Una serie di **3 anelli rotanti** a induzione magnetica attorno al deflettore di coda, responsabili della coerenza del campo FTL.
6.  **Structural Pylons (Support Arms)**: Bracci di sostegno affusolati a forma di ellissoide che connettono rigidamente la sezione ingegneria alle unità di propulsione.
7.  **Hyperdrive Nacelles (FTL Units)**: Le gondole gemelle laterali, generatori primari della bolla di curvatura necessari per il volo spaziale superluminale.

Questa architettura allungata e priva di collegamenti sottili ("Collo") rappresenta l'evoluzione tecnologica dell'Alleanza verso vascelli più robusti e resistenti agli impatti cinetici.

---

## 💾 Persistenza e Continuità
L'architettura di Space GL è progettata per sostenere una galassia persistente e dinamica. Ogni azione, dalla scoperta di un nuovo sistema planetario al caricamento della Cargo Bay, viene preservata tramite un sistema di archiviazione binaria a basso livello.

#### 🗄️ Il Database Galattico (`galaxy.dat`)
Il file `galaxy.dat` costituisce la memoria storica del simulatore. Utilizza una struttura di **Serializzazione Diretta** della memoria RAM del server:
*   **Galaxy Master Matrix**: Una griglia tridimensionale 10x10x10 che memorizza la densità di massa e la composizione di ogni quadrante (codifica BPNBS).
*   **Registri Entità**: Un dump completo degli array globali (`npcs`, `stars_data`, `planets`, `bases`), preservando coordinate relative, livelli di energia e timer di cooldown.
*   **Integrità dei Dati**: Implementa un controllo di versione rigido (`GALAXY_VERSION`). Se il server rileva un file generato con parametri strutturali diversi (es. numero massimo di NPC variato), invalida il caricamento per prevenire corruzioni di memoria, rigenerando un universo coerente.

#### 🔄 Pipeline di Sincronizzazione (Auto-Save)
La continuità è garantita da un loop di sincronizzazione asincrona:
*   **Flush Periodico**: Ogni 60 secondi, il thread logico avvia una procedura di salvataggio.
*   **Thread Safety**: Durante l'operazione di I/O su disco, il sistema acquisisce il `game_mutex`. Questo assicura che il database salvato sia uno **snapshot atomico** e coerente dell'intero universo in quel preciso istante temporale.

#### 🆔 Identità e Restauro Profilo
Il sistema di continuità per i giocatori si basa sulla **Persistent Identity**:
*   **Riconoscimento**: Inserendo lo stesso nome capitano utilizzato in precedenza, il server interroga il database dei giocatori attivi e storici.
*   **Session Recovery**: Vengono ripristinati istantaneamente le coordinate globali, l'inventario strategico e lo stato dei sistemi.

#### 🆘 Protocollo EMERGENCY RESCUE (Salvataggio d'Emergenza)
In caso di distruzione del vascello, al successiva accesso il Comando dell'Alleanza attiva un protocollo di recupero: ripristino sistemi all'80%, rifornimento di emergenza e rilocazione in un settore sicuro.

Questa architettura garantisce che Space GL non sia solo una sessione di gioco, ma una vera e propria carriera spaziale in evoluzione.

## 🔐 Crittografia Dello spazio profondo: Approfondimento Tattico

Space GL implementa una suite crittografica multi-livello che trasforma la sicurezza delle comunicazioni in una vera e propria meccanica di gioco tattica. Ogni algoritmo rappresenta una "frequenza" operativa differente.

### 📡 Protocolli di Trasmissione e Autenticazione

Oltre alla scelta dell'algoritmo, il sistema GDIS utilizza protocolli avanzati per garantire che ogni ordine provenga dal legittimo comandante:

*   **Initial Handshake (XOR Obfuscation)**: Al momento della connessione, il client e il server negoziano una **Session Key** unica a 256-bit. Questo scambio avviene tramite un protocollo di offuscamento XOR basato sulla **Master Key** del settore (`SPACEGL_KEY`), garantendo che nessun pacchetto sia leggibile senza l'autorizzazione iniziale.
*   **Firme Digitali Ed25519**: Ogni pacchetto inviato via radio (`rad`) è firmato digitalmente. Il ricevente verifica istantaneamente l'autenticità tramite curve ellittiche. I messaggi autentici sono contrassegnati da **`[VERIFIED]`** in verde.
*   **Rotating Frequency Integration**: Il vettore di inizializzazione (IV) di ogni messaggio viene modificato dinamicamente in base al `frame_id` del server. Questo rende il sistema immune ai *Replay Attacks*: un messaggio registrato un secondo prima risulterà illeggibile il secondo dopo.

### ⚛️ Suite degli Algoritmi (Frequenze Operative)

Il comando `enc <ALGO>` permette di sintonizzare i sistemi di bordo su uno dei seguenti standard:

#### 1. ML-KEM-1024 (Kyber) - `enc pqc`
*   **Descrizione**: Crittografia Post-Quantistica basata su problemi di reticoli (Lattice-based).
*   **Uso Tattico**: È l'apice della segretezza galattica. Utilizzato dalla **Sezione Ombra** per comunicazioni che devono rimanere protette anche contro futuri attacchi da computer quantistici. Inattaccabile con tecnologie convenzionali.

#### 2. AES-256-GCM - `enc aes`
*   **Descrizione**: Advanced Encryption Standard con Galios/Counter Mode.
*   **Uso Tattico**: Lo standard ufficiale del **Comando dell'Alleanza**. Offre il miglior bilanciamento tra sicurezza estrema e velocità, grazie all'accelerazione hardware dei processori Synaptics. Include l'autenticazione del messaggio (AEAD).

#### 3. ChaCha20-Poly1305 - `enc chacha`
*   **Descrizione**: Cifrario a flusso moderno abbinato a un autenticatore di messaggi.
*   **Uso Tattico**: Preferito dai vascelli di classe **Scout** ed **Escort**. Estremamente veloce in ambienti dove la potenza di calcolo è limitata, garantendo latenza minima nei collegamenti tattici.

#### 4. ARIA-256-GCM - `enc aria`
*   **Descrizione**: Standard di crittografia a blocchi sudcoreano certificato.
*   **Uso Tattico**: Rappresenta la frequenza di coalizione tra l'**Alleanza** e l'**Impero Korthian**. Utilizzato per operazioni congiunte su vasta scala.

#### 5. Camellia-256-CTR - `enc camellia`
*   **Descrizione**: Cifrario a blocchi ad alta efficienza di origine terrestre (giapponese).
*   **Uso Tattico**: Lo standard imperiale dell'**Impero Stellare Xylari**. Noto per la sua eleganza matematica e resistenza ai tentativi di decrittazione a forza bruta degli infiltrati.

#### 6. IDEA-CBC - `enc idea`
*   **Descrizione**: International Data Encryption Algorithm.
*   **Uso Tattico**: La frequenza della **Resistenza** e dei gruppi indipendenti. Resiliente e difficile da analizzare per i computer centralizzati delle grandi potenze.

#### 7. Blowfish-CBC - `enc bf`
*   **Descrizione**: Progettato per essere veloce e compatto.
*   **Uso Tattico**: Protocollo commerciale standard del **Cartello Dorato**. Utilizzato per proteggere le transazioni di Aetherium e i manifesti di carico.

#### 8. CAST5-CBC - `enc cast`
*   **Descrizione**: Algoritmo a chiave variabile (fino a 128 bit).
*   **Uso Tattico**: Utilizzato nelle regioni di confine per comunicazioni civili e governative locali.

#### 9. Triple DES (3DES) - `enc 3des`
*   **Descrizione**: Tripla applicazione del Data Encryption Standard.
*   **Uso Tattico**: Frequenza di "Legacy". Utilizzata per accedere agli archivi storici e comunicare con antiche stazioni spaziali automatizzate.

#### 10. SEED-CBC - `enc seed`
*   **Descrizione**: Cifrario a blocchi a 128 bit.
*   **Uso Tattico**: Utilizzato principalmente nei protocolli industriali e logistici pesanti dell'Unione Vesperiana.

#### 11. RC4 Stream - `enc rc4`
*   **Descrizione**: Cifrario a flusso storico.
*   **Uso Tattico**: Sebbene considerato insicuro per segreti di stato, è utilizzato per link telemetrici grezzi a bassissima latenza dove la velocità è l'unica priorità.

#### 12. DES-CBC - `enc des`
*   **Descrizione**: Lo standard originale degli anni '70 terrestri.
*   **Uso Tattico**: Mappato sui **segnali pre-Hyperdrive**. Necessario per decrittare comunicazioni provenienti da antiche sonde sleeper o segnali di civiltà ai primi stadi tecnologici.

---
*SPACE GL - 3D LOGIC ENGINE. Sviluppato con eccellenza tecnica da Nicola Taibi. "Per Tenebras, Lumen"*
