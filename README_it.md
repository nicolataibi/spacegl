# Space GL: 3D Multi-User Client-Server Edition
## Un gioco di esplorazione e combattimento spaziale
## "Per Tenebras, Lumen" ("Attraverso le tenebre, la luce")
### Website: https://github.com/nicolataibi/spacegl
### Authors: Nicola Taibi, Supported by Google Gemini
### Copyright (C) 2026 Nicola Taibi - Licensed under GPL-3.0-or-later
**Persistent Galaxy Tactical Navigation & Combat Simulator**

<table>
  <tr>
    <td><img src="readme_assets/StellarAlliance-LegacyClass.jpg" alt="Stellar Alliance Legacy Class" width="400"/></td>
    <td><img src="readme_assets/startup.jpg" alt="Space GL Startup" width="400"/></td>
  </tr>
  <tr>
    <td><img src="readme_assets/running.jpg" alt="Space GL Running" width="400"/></td>
    <td><img src="readme_assets/combat.jpg" alt="Space Combat" width="400"/></td>
  </tr>
  <tr>
    <td><img src="readme_assets/comets.jpg" alt="Comets and Asteroids" width="400"/></td>
    <td><img src="readme_assets/space-monster.jpg" alt="Space Monster" width="400"/></td>
  </tr>
  <tr>
    <td><img src="readme_assets/rift.jpg" alt="Spatial Rift" width="400"/></td>
    <td><img src="readme_assets/shields.jpg" alt="Defensive Shields" width="400"/></td>
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

### 3. Avvio del Server
Lancia lo script di avvio sicuro. Ti verrà chiesto di impostare una **Master Key** (password segreta per il server):
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
*   **Spatial Partitioning**: Utilizza un indice spaziale 3D (Grid Index) per la gestione degli oggetti. Questo permette al server di scansionare solo gli oggetti locali al giocatore, garantendo prestazioni costanti ($O(1)$) indipendentemente dal numero totale di entità nella galassia.
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
    *   `list <q1> <q2> <q3>`: Fornisce un censimento millimetrico di un singolo quadrante, rivelando coordinate precise, tipi di risorse (es. Aetherium, Neo-Titanium) e classi di navi per tutte le entità.
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
    *   Attivabile tramite il comando `map`, questa modalità trasforma la vista tattica in una mappa galattica globale 10x10x10.
    *   **Legenda Olografica Oggetti**: La mappa fornisce una proiezione olografica ad alta risoluzione del settore, utilizzando simboli specifici e codifica cromatica per identificare le entità a colpo d'occhio:
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
    *   **Combat FX**: Visualizzazione in tempo reale di raggi Ion Beam gestiti via **GLSL Shader**, siluri al plasma con bagliore dinamico ed esplosioni volumetriche.
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

Questo implementazione permette al simulatore di scalare fluidamente, mantenendo la latenza di comando (Input Lag) minima e la coerenza della galassia assoluta per tutti i capitani connessi.

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
*   **Predictive AI Update**: Calcolo dei vettori di movimento per gli NPC basato su matrici di inseguimento e pesi tattici (fazione, energia residua).
*   **Spatial Indexing (Grid Partitioning)**: Gli oggetti non vengono iterati linearmente ($O(N)$), ma mappati in una griglia tridimensionale 10x10x10. Questo riduce la complessità delle collisioni e dei sensori a ($O(1)$) per l'area locale del giocatore.
*   **Physics Enforcement**: Applicazione del clamping galattico e risoluzione delle collisioni con corpi celesti statici.

### 2. ID-Based Object Tracking & Shared Memory Mapping
Il sistema di tracciamento degli oggetti utilizza un'architettura a **Identificativi Persistenti**:
*   **Server Side**: Ogni entità (nave, stella, pianeta) ha un ID univoco globale. Durante il tick, solo gli ID visibili al giocatore vengono serializzati.
*   **Client/Viewer Side**: Il visualizzatore mantiene un buffer locale di 200 slot. Attraverso una **Hash Map implicita**, il client associa l'ID del server a uno slot SHM. Se un ID scompare dal pacchetto di rete, il sistema di *Stale Object Purge* invalida istantaneamente lo slot locale, garantendo la coerenza visiva senza latenza di timeout.

### 3. Modello di Networking Avanzato (Atomic Binary Stream)
Per garantire la stabilità su reti ad alta frequenza, il simulatore implementa:
*   **Atomic Packet Delivery:** Ogni connessione client è protetta da un `socket_mutex` dedicato. Questo garantisce che i pacchetti di grandi dimensioni (come la Galassia Master) non vengano mai interrotti o mescolati con i pacchetti di aggiornamento logico, eliminando alla radice la corruzione del flusso binario e le race conditions.
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

### 6. Robustezza e Session Continuity
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
*   **Pianeti**: Corpi celesti ricchi di minerali. Possono essere scansionati ed estratti (`min`) per Aetherium, Neo-Titanium e altre risorse vitali.
*   **Buchi Neri (Black Holes)**: Singolarità gravitazionali con dischi di accrezione. Sono la fonte primaria di Plasma Reserves (`har`), ma la loro attrazione può essere fatale.
*   **Nebulose (Nebulas)**: Grandi nubi di gas ionizzati (Standard, High-Energy, Dark Matter, ecc.). Forniscono copertura tattica (occultamento naturale) ma disturbano i sensori.
*   **Pulsar**: Stelle di neutroni che emettono radiazioni letali. Navigare troppo vicino danneggia i sistemi e l'equipaggio.
*   **Comete (Comets)**: Oggetti in movimento veloce con code volumetriche. Possono essere analizzate per raccogliere gas rari.
*   **Faglie Spaziali (Spatial Rifts)**: Strappi nel tessuto spaziotemporale. Fungono da teletrasporti naturali che proiettano la nave in un punto casuale della galassia.

### 🚩 Fazioni e Navi Intelligenti
*   **Alleanza (Player/Starbase)**: Include la tua nave e le Basi Stellari, dove puoi attraccare (`doc`) per riparazioni e rifornimenti completi.
*   **Impero Korthian**: Guerrieri aggressivi che pattugliano i quadranti, spesso protetti da piattaforme difensive.
*   **Impero Stellare Xylario**: Maestri dell'inganno che utilizzano l'occultamento per lanciare attacchi a sorpresa.
*   **Collettivo Swarm**: La minaccia più grande. I loro Cubi hanno una potenza di fuoco massiccia e capacità rigenerative superiori.
*   **Fazioni NPC**: Vesperiani, Ascendant, Quarzitei, Saurian, Gilded, Fluidic Void, Cryos e Apex. Ognuna con diversi livelli di ostilità e potenza.

#### ⚖️ Sistema di Fazioni e Protocollo "Traditore" (Renegade)
Space GL implementa un sistema di reputazione dinamico che gestisce le relazioni tra il giocatore e le diverse potenze galattiche.

*   **Riconoscimento IFF Alleato**: Al momento della creazione del personaggio, il capitano sceglie una fazione di appartenenza. Le navi NPC e le piattaforme di difesa della stessa fazione riconosceranno il vascello come alleato e **non apriranno il fuoco a vista**.
*   **Fuoco Amico e Tradimento**: Se un giocatore attacca deliberatamente un'unità della propria fazione (nave, base o piattaforma):
    *   **Stato Renegade**: Il capitano viene immediatamente marcato come **TRADITORE (Renegade)**.
    *   **Ritorsione Immediata**: Tutte le unità della propria fazione nel settore diventeranno ostili e inizieranno manovre di attacco pesanti.
    *   **Messaggio di Allerta**: Il computer di bordo riceverà un avviso critico: `FRIENDLY FIRE DETECTED! You have been marked as a TRAITOR by the fleet!`.
*   **Durata e Amnistia**: Lo status di traditore è severo ma temporaneo. Ha una durata di **10 minuti (tempo reale)**.
    *   Durante questo periodo, ogni ulteriore attacco contro alleati resetterà il timer.
    *   Allo scadere del tempo, se non sono state commesse altre ostilità, il Comando di Settore concederà l'amnistia: `Amnesty granted. Your status has been restored to active duty.` e le unità della fazione torneranno ad essere neutrali/alleate.

### ⚠️ Pericoli e Risorse Tattiche
*   **Campi di Asteroidi**: Detriti rocciosi che rappresentano un rischio fisico. Il danno da collisione aumenta con la velocità della nave.
*   **Mine Spaziali**: Ordigni esplosivi occulti piazzati da fazioni ostili. Rilevabili solo tramite scansione ravvicinata.
*   **Relitti alla Deriva (Derelicts)**: Gusci di navi distrutte. Possono essere smantellati (`dis`) per recuperare componenti e risorse.
*   **Boe di Comunicazione**: Nodi della rete del Comando dell'Alleanza. Trovarsi vicino potenzia i sensori a lungo raggio (`lrs`), fornendo dati dettagliati sulla composizione dei quadranti adiacenti.
*   **Piattaforme Difensive (Turrets)**: Sentinelle automatiche pesantemente armate che proteggono aree di interesse strategico.

### 👾 Anomalie e Creature
*   **Mostri Spaziali**: Include l'**Entità Cristallina** e l'**Ameba Spaziale**, creature uniche che cacciano attivamente i vascelli per nutrirsi della loro energia.
*   **Tempeste Ioniche**: Fenomeni meteorologici che si spostano nella galassia, capaci di accecare i sensori e deviare la rotta delle navi.

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
    *   `S`: Velocità (0.0 - 1.0).
    *   `imp 0 0 0`: All Stop (Arresto).
*   `cal <QX> <QY> <QZ> [SX SY SZ]`: **Computer di Navigazione (Alta Precisione)**. Genera un rapporto completo con Heading, Mark e una **Tabella di Confronto delle Velocità**. Se vengono fornite le coordinate di settore (SX, SY, SZ), calcola la rotta precisa verso quella posizione.
*   `ical <X> <Y> <Z>`: **Calcolatore d'Impulso (ETA)**. Calcola H, M ed ETA per raggiungere coordinate precise (0.0-10.0) all'interno del quadrante attuale, basandosi sull'attuale allocazione di potenza ai motori.
    
    *   `jum <QX> <QY> <QZ>`: **Salto Wormhole (Ponte di Einstein-Rosen)**.
     Genera un wormhole per un salto istantaneo verso il quadrante di destinazione.
    *   **Requisiti**: 5000 unità di Energia e 1 Cristallo di Aetherium.
    *   **Procedura**: Richiede una sequenza di stabilizzazione della singolarità di circa 3 secondi.
*   `apr [ID] [DIST]`: **Autopilota di Avvicinamento**. Avvicinamento automatico al bersaglio ID fino alla distanza DIST.
    *   Se non viene fornito un ID, utilizza il **bersaglio attualmente agganciato**.
    *   Se viene fornito un solo numero, viene interpretato come **distanza** per il bersaglio agganciato (se < 100).
*   `cha`: **Autopilota di Inseguimento**. Insegue attivamente il bersaglio agganciato, mantenendo la traiettoria di intercettazione.
*   `rad <MSG>`: **Radio Dello spazio profondo**. Invia un messaggio globale. Usa `@Fazione` per chat di squadra o `#ID` per messaggi privati.
*   `doc`: **Attracco**. Attracca a una Base Stellare (richiede distanza ravvicinata).
*   `map [FILTRO]`: **Cartografia Stellare**. Attiva la visualizzazione 3D globale 10x10x10 dell'intera galassia.
    *   **Filtri Opzionali**: Puoi visualizzare solo categorie specifiche usando: `map st` (Stelle), `map pl` (Pianeti), `map bs` (Basi), `map en` (Nemici), `map bh` (Buchi Neri), `map ne` (Nebulose), `map pu` (Pulsar), `map is` (Tempeste), `map co` (Comete), `map as` (Asteroidi), `map de` (Relitti), `map mi` (Mine), `map bu` (Boe), `map pf` (Piattaforme), `map ri` (Rift), `map mo` (Mostri).
    *   **HUD Verticale**: In modalità mappa, una legenda a sinistra mostra i colori e i codici filtro per ogni oggetto.
    *   **Anomalie Dinamiche**:
        *   **Tempeste Ioniche**: Quadranti racchiusi in un guscio wireframe bianco trasparente.
        *   **Supernova**: Un **grande cubo rosso pulsante** indica un'imminente esplosione stellare nel quadrante (pericolo estremo).
    *   **Localizzazione**: La posizione attuale della nave è indicata da un **indicatore bianco pulsante**, facilitando la navigazione a lungo raggio.

### 🔬 Sensori e Scanner
*   `scan <ID>`: **Analisi Scansione Profonda**. Esegue una scansione profonda del bersaglio o dell'anomalia.
    *   **Vascelli**: Rivela l'integrità dello scafo, i livelli degli scudi per quadrante, l'energia residua, il numero dell'equipaggio e i danni ai sottosistemi.
    *   **Anomalie**: Fornisce dati scientifici su Nebulose e Pulsar.

#### 📡 Integrità dei Sensori e Precisione dei Dati
L'efficacia dei tuoi sensori dipende direttamente dallo stato di salute del **sistema Sensori (ID 2)**:
*   **Salute 100%**: Dati precisi e affidabili.
*   **Salute < 100%**: Introduzione di "rumore" telemetrico. Le coordinate di settore `[X,Y,Z]` mostrate in `srs` diventano imprecise (l'errore aumenta esponenzialmente al diminuire della salute).
*   **Salute < 50%**: Il comando `lrs` inizia a visualizzare dati corrotti o incompleti sui quadranti circostanti.
*   **Salute < 30%**: Rischio di "Target Ghosting" o mancata rilevazione di oggetti reali.
*   **Riparazione**: Usa `rep 2` per ripristinare la precisione nominale dei sensori.

*   `srs`: **Sensori a Corto Raggio**. Scansione dettagliata del quadrante attuale.
    *   **Scansione del Vicinato**: Se la nave è vicina ai confini del settore (< 2.5 unità), i sensori rilevano automaticamente gli oggetti nei quadranti adiacenti, elencandoli in una sezione dedicata per prevenire imboscate.
*   `lrs`: **Sensori a Lungo Raggio**. Scansione 3x3x3 dei quadranti circostanti visualizzata tramite **Console Tattica GDIS**.
    *   **Layout**: Ogni quadrante è visualizzato su una singola riga per un'immediata leggibilità (Coordinate, Navigazione, Oggetti e Anomalie).
    *   **Dati Standard**: Mostra solo la presenza di oggetti usando le loro iniziali (es. `[H . N . S]`).
    *   **Dati Potenziati**: Se la nave è vicino a una **Boa di Comunicazione** (< 1.2 unità), i sensori passano alla visualizzazione numerica rivelando il conteggio esatto (es. `[1 . 2 . 8]`). Il potenziamento si resetta quando ci si allontana dalla boa.
    *   **Soluzione di Navigazione**: Ogni quadrante include i parametri `H / M / W` calcolati per raggiungerlo immediatamente.
    *   **Legenda Primaria**: `[ H P N B S ]` (Buchi Neri, Pianeti, NPC, Basi, Stelle).
    *   **Simbologia Anomalie**: `~`:Nebulosa, `*`:Pulsar, `+`:Cometa, `#`:Asteroide, `M`:Mostro, `>`:Rift.
    *   **Localizzazione**: Il tuo quadrante attuale è evidenziato con uno sfondo blu.
*   `aux probe <QX> <QY> <QZ>`: **Sonda Sensoriale Dello spazio profondo**. Lancia una sonda automatizzata in un quadrante specifico.
    *   **Entità Galattica**: Le sonde sono oggetti globali. Attraversano i quadranti intermedi in tempo reale e sono **visibili a tutti i giocatori** lungo la loro rotta di volo.
    *   **Integrazione Sensori**: Le sonde appaiono nella lista **SRS (Sensori a Corto Raggio)** per qualsiasi nave nello stesso settore (ID range 19000+), rivelando il **Nome del Proprietario** e lo stato attuale.
    *   **Funzionalità**: Visualizza l'**ETA** e lo stato della missione nell'HUD del proprietario.
    *   **Recupero Dati**: All'arrivo, rivela la composizione del quadrante (`H P N B S`) nella mappa del proprietario e invia un rapporto telemetrico in diretta.
    *   **Persistenza**: Rimane nella posizione bersaglio come un **Relitto** (Anelli rossi) dopo la missione.
    *   **Comando `aux report <1-3>`**: Richiede un nuovo aggiornamento sensoriale da una sonda attiva.
    *   **Comando `aux recover <1-3>`**: Recupera una sonda se la nave è nello stesso quadrante e a portata (< 2.0 unità), liberando lo slot e ripristinando 500 unità di energia.
*   `sta`: **Rapporto di Stato**. Rapporto completo sullo stato della nave, la missione e il monitoraggio dell'**Equipaggio**.
*   `dam`: **Rapporto Danni**. Dettaglio dei danni ai sistemi.
*   `who`: Elenco dei capitani attivi nella galassia.

### ⚔️ Combattimento Tattico
*   `pha <E>`: **Fuoco Ion Beam**. Spara Ion Beam al bersaglio agganciato (`lock`) usando l'energia E. 
*   `pha <ID> <E>`: Spara Ion Beam a uno specifico bersaglio ID. Il danno diminuisce con la distanza.
*   `cha`: **Inseguimento**. Insegue e intercetta automaticamente il bersaglio agganciato.
*   `rad <MSG>`: **Radio**. Invia un messaggio Dello spazio profondo ad altri capitani (@Fazione per chat di squadra).
*   `axs` / `grd`: **Guide Visive**. Attiva/disattiva gli assi 3D o la griglia tattica sovrapposta.
*   `bridge [top/bottom/up/down/left/right/rear/off]`: **Vista Ponte**. Attiva una vista cinematografica in prima persona.
    *   `top/on`: Vista ponte standard sopra la cupola di comando.
    *   `bottom`: Prospettiva da sotto lo scafo.
    *   `up/down/left/right/rear`: Cambia la direzione dello sguardo mantenendo la posizione attuale (sopra/sotto).
*   `enc <algo>`: **Commutazione Crittografia**. Abilita o disabilita la crittografia in tempo reale. Supporta **AES-256-GCM**, **ChaCha20**, **ARIA**, **Camellia**, **Blowfish**, **RC4**, **CAST5**, **IDEA**, **3DES** e **PQC (ML-KEM)**. Essenziale per proteggere le comunicazioni e leggere i messaggi sicuri degli altri capitani.
*   `tor`: **Lancio Siluro al plasma**. Lancia un siluro autoguidato al bersaglio agganciato.
*   `tor <H> <M>`: Lancia un siluro in modalità balistica manuale (Heading/Mark).
*   `lock <ID>`: **Aggancio Bersaglio**. Aggancia i sistemi di puntamento sul bersaglio ID (0 per sbloccare). Essenziale per la guida automatizzata di Ion Beam e siluri.


### 🆔 Schema Identificativi Galattici (Universal ID)
Per interagire con gli oggetti galattici usando i comandi `lock`, `scan`, `pha`, `tor`, `bor` e `dis`, il sistema utilizza un sistema di ID unico. Usa il comando `srs` per identificare gli ID degli oggetti nel tuo settore.

| Categoria | Intervallo ID | Esempio | Utilizzo Primario |
| :--- | :--- | :--- | :--- |
| **Giocatore** | 1 - 999 | `lock 1` | Tuo vascello o altri giocatori |
| **NPC (Nemico)** | 1.000 - 1.999 | `lock 1050` | Inseguimento (`cha`) e combattimento |
| **Basi Stellari** | 2.000 - 2.999 | `lock 2005` | Attracco (`doc`) e rifornimento |
| **Pianeti** | 3.000 - 3.999 | `lock 3012` | Estrazione planetaria (`min`) |
| **Stelle** | 4.000 - 6.999 | `lock 4500` | Ricarica solare (`sco`) |
| **Buchi Neri** | 7.000 - 7.999 | `lock 7001` | Raccolta Plasma Reserves (`har`) |
| **Nebulose** | 8.000 - 8.999 | `lock 8000` | Analisi scientifica e copertura |
| **Pulsar** | 9.000 - 9.999 | `lock 9000` | Monitoraggio radiazioni |
| **Comete** | 10.000 - 10.999| `lock 10001` | Inseguimento e raccolta gas rari |
| **Relitti** | 11.000 - 11.999| `lock 11005` | Abbordaggio (`bor`) e recupero tech |
| **Asteroidi** | 12.000 - 13.999| `lock 12000` | Navigazione di precisione |
| **Mine** | 14.000 - 14.999| `lock 14000` | Allerta tattica ed evitamento |
| **Boe Comm.** | 15.000 - 15.999| `lock 15000` | Link dati e potenziamento `lrs` |
| **Piattaforme** | 16.000 - 16.999| `lock 16000` | Distruzione sentinelle ostili |
| **Rift Spaziali** | 17.000 - 17.999| `lock 17000` | Utilizzo per salti casuali |
| **Mostri** | 18.000 - 18.999| `lock 18000` | Scenari di combattimento estremo |
| **Sonde** | 19.000 - 19.999| `scan 19000` | Raccolta dati automatizzata |

**Nota**: L'aggancio funziona solo se l'oggetto è nel tuo quadrante attuale. Se l'ID esiste ma è lontano, il computer indicherà le coordinate `Q[x,y,z]` del bersaglio.

### 🔄 Workflow Tattico Raccomandato
Per eseguire operazioni complesse (estrazione, rifornimento, abbordaggio), segui questa sequenza ottimizzata:

1.  **Identificazione**: Usa `srs` per trovare l'ID dell'oggetto (es. Stella ID `4226`).
2.  **Aggancio (Lock-on)**: Esegui `lock 4226`. Vedrai l'ID confermato sul tuo HUD 3D.
3.  **Avvicinamento**: Usa `apr 4226 1.5`. L'autopilota ti porterà alla distanza di interazione ideale.
4.  **Interazione**: Una volta arrivato, lancia il comando specifico:
    *   `sco` per le **Stelle** (Ricarica energia).
    *   `min` per i **Pianeti** (Estrazione mineraria).
    *   `har` per i **Buchi Neri** (Raccolta Plasma Reserves).
    *   `bor` per i **Relitti** (Recupero tecnologico e riparazioni).
    *   `cha` per le **Comete** (Inseguimento e raccolta gas).
    *   `pha` / `tor` per **Nemici/Mostri/Piattaforme** (Combattimento).

### 📏 Tabella delle Distanze di Interazione
Distanze espresse in unità di settore (0.0 - 10.0). Se la tua distanza è superiore al limite, il computer risponderà con "No [object] in range".

| Oggetto / Entità | Comando / Azione | Distanza Minima | Effetto / Interazione |
| :--- | :--- | :--- | :--- |
| **Stella** | `sco` | **< 2.0** | Ricarica solare (Solar scooping) |
| **Pianeta** | `min` | **< 2.0** | Estrazione planetaria |
| **Base Stellare** | `doc` | **< 2.0** | Riparazione completa, ricarica energia e siluri |
| **Buco Nero** | `har` | **< 2.0** | Raccolta Plasma Reserves |
| **Relitto** | `dis` | **< 1.5** | Smantellamento per risorse |
| **Nave Nemica** | `bor` | **< 1.0** | Operazione squadra d'abbordaggio |
| **Nave Nemica** | `pha` (Fuoco) | **< 6.0** | Gittata massima Ion Beam NPC |
| **Siluro al plasma** | (Impatto) | **< 0.5** | Distanza di collisione per detonazione |
| **Boa Comm.** | (Passivo) | **< 1.2** | Potenziamento segnale o messaggi auto |
| **Ameba Spaziale** | (Contatto) | **< 1.5** | Inizio drenaggio energetico critico |
| **Entità Cristallina**| (Risonanza) | **< 4.0** | Gittata del raggio di risonanza |
| **Corpo Celeste** | (Collisione) | **< 1.0** | Danni scafo e attivazione soccorso d'emergenza |

### 🚀 Autopilota (`apr`)
Il comando `apr <ID> <DIST>` ti permette di avvicinarti automaticamente a qualsiasi oggetto rilevato dai sensori. Per le entità mobili, l'intercettazione funziona in tutta la galassia.

| Categoria Oggetto | Intervallo ID | Comandi di Interazione | Dist. Min. | Note di Navigazione |
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
| **Relitti** | 11000 - 11149 | `bor`, `dis`, `scan` | **< 1.5** | Solo quadrante attuale |
| **Asteroidi** | 12000 - 13999 | `min`, `scan` | **< 3.1** | Solo quadrante attuale |
| **Mine** | 14000 - 14999 | `scan` | - | Solo quadrante attuale |
| **Boe Comm.** | 15000 - 15099 | `scan` | **< 1.2** | Solo quadrante attuale |
| **Piattaforme Difesa** | 16000 - 16199 | `pha`, `tor`, `scan` | - | Solo quadrante attuale |
| **Rift Spaziali** | 17000 - 17049 | `scan` | - | Solo quadrante attuale |
| **Mostri Spaziali** | 18000 - 18029 | `pha`, `tor`, `scan` | **< 1.5** | **Tracciamento Galattico** |

*   `she <F> <R> <T> <B> <L> <RI>`: **Configurazione Scudi**. Distribuisce l'energia ai 6 scudi.
*   `clo`: **Dispositivo di Occultamento**. Attiva/Disattiva l'occultamento. Consuma 15 unità di energia/tick. Fornisce invisibilità agli NPC e alle altre fazioni; instabile nelle nebulose.
*   `pow <E> <S> <W>`: **Allocazione di Potenza**. Alloca l'energia del reattore (Motori, Scudi, Armi %).
*   `aux jettison`: **Espulsione Synaptics Hyperdrive**. Espelle il nucleo (Manovra suicida / Ultima risorsa).
*   `xxx`: **Autodistruzione**. Autodistruzione sequenziale.

### ⚡ Gestione del Reattore e della Potenza

Il comando `pow` è fondamentale per la sopravvivenza e la superiorità tattica. Detto comando determina come l'uscita del reattore principale della nave viene ripartita su tre sottosistemi principali:

*   **Motori (E)**: Influenza la reattività e la velocità massima del **Motore a Impulso**. Un'alta allocazione permette manovre rapide e un attraversamento più veloce del settore.
*   **Scudi (S)**: Governa il **Tasso di Rigenerazione** di tutti i 6 quadranti degli scudi. Se gli scudi sono danneggiati, attingono energia dal reattore per ricostruire la loro integrità.
    *   **Scaling Dinamico**: La velocità di rigenerazione è un prodotto sia della **Potenza (S)** assegnata che dell'**Integrità del Sistema Scudi**. Se il generatore di scudi è danneggiato, la rigenerazione sarà gravemente ostacolata indipendentemente dall'allocazione di potenza.

#### 🛡️ Meccanica degli Scudi e Integrità dello Scafo
La nave è protetta da 6 quadranti indipendenti: **Frontale (F), Posteriore (R), Superiore (T), Inferiore (B), Sinistro (L) e Destro (RI)**.

*   **Danni Localizzati**: Gli attacchi (Ion Beam/Siluri) ora colpiscono quadranti specifici in base all'angolo relativo di impatto.
*   **Integrità dello Scafo**: Rappresenta la salute fisica della nave (0-100%). Se un quadrante di scudo raggiunge lo 0% o l'impatto è eccessivamente potente, il danno residuo colpisce direttamente l'integrità strutturale.
*   **Danni ai Sistemi Interni**: Quando lo scafo viene colpito direttamente (scudi a zero), c'è un'alta probabilità di subire danni ai sottosistemi (motori, armi, sensori, ecc.).
    *   **Ion Beam**: Moderata possibilità di danni casuali ai sistemi.
    *   **Siluri**: Altissima probabilità (>50%) di guasti critici al sistema all'impatto.
*   **Placcatura dello Scafo (Composite)**: Una placcatura aggiuntiva (comando `hull`) funge da buffer: assorbe i danni fisici *prima* che colpiscano l'integrità dello scafo.
*   **Condizione di Distruzione**: Se l'**Integrità dello Scafo raggiunge lo 0%**, la nave esplode istantaneamente, indipendentemente dall'energia rimanente o dai livelli degli scudi.
*   **Rigenerazione Continua**: A differenza dei vecchi sistemi, la rigenerazione degli scudi è continua ma scala con lo stato dell'hardware.
*   **Fallimento dello Scudo**: Se un quadrante raggiunge lo 0% di integrità, i successivi colpi da quella direzione infliggeranno danni diretti allo scafo e al reattore energetico principale.

#### 🛸 Dispositivo di Occultamento
Il comando `clo` attiva un'avanzata tecnologia di occultamento che manipola la luce e i sensori per rendere il vascello invisibile.

*   **Invisibilità Tattica**: Una volta attivo, non sarai rilevabile dai sensori (`srs`/`lrs`) degli altri giocatori (a meno che non appartengano alla tua stessa fazione). Le navi NPC ti ignoreranno completamente e non inizieranno manovre di attacco.
*   **Costi Energetici**: Mantenere il campo di occultamento è estremamente dispendioso in termini energetici, consumando **15 unità di energia per tick logico**. Monitora attentamente le riserve del reattore.
*   **Limitazioni dei Sensori**: Durante l'occultamento, i sensori di bordo subiscono interferenze ("Sensors limited"), rendendo più difficile l'acquisizione di dati ambientali precisi.
*   **Instabilità nelle Nebulose**: All'interno delle nebulose, il campo di occultamento diventa instabile a causa dei gas ionizzati. Questo può causare fluttuazioni nel drenaggio energetico e inibire la rigenerazione degli scudi.
*   **Feedback Visivo**: Quando occultato, il modello originale della nave scompare e viene sostituito da una mesh wireframe con un effetto **"Blue Glowing"** pulsante. L'HUD visualizzerà lo stato `[ CLOAKED ]` in magenta.
*   **Restrizioni al Combattimento**: Non puoi sparare **Ion Beam** (`pha`) o lanciare **Siluri** (`tor`) mentre il dispositivo di occultamento è attivo. Devi disattivare l'occultamento per ingaggiare il nemico.
*   **Strategia NPC (Xylari)**: L'Impero Stellare Xylari utilizza tattiche di occultamento avanzate; le loro navi rimarranno occultate durante il pattugliamento o la fuga, rivelandosi solo per lanciare un attacco.

*   **Armi (W)**: Scala direttamente l'**Intensità del Raggio Ion Beam** e il **Tasso di Ricarica**. Un'allocazione più alta si traduce in più energia focalizzata nei banchi Ion Beam, infliggendo danni esponenzialmente maggiori e permettendo al condensatore Ion Beam di ricaricarsi molto più velocemente.

#### 🎯 Nota Tactical Ordnance: Ion Beam e Siluri
*   **Condensatore Ion Beam**: Visibile nell'HUD come "Ion Beam CAPACITOR: XX%". Rappresenta l'energia attualmente immagazzinata nei banchi d'arma.
    *   **Fuoco**: Ogni raffica Ion Beam consuma una parte del condensatore basata sull'impostazione dell'energia. Se il condensatore è sotto il 10%, non puoi sparare.
    *   **Ricarica**: Si ricarica automaticamente ogni secondo. La velocità di ricarica è potenziata direttamente assegnando più potenza alle **Armi (W)** tramite il comando `pow`.
*   **Integrità Ion Beam**: Nell'HUD come "Ion Beam INTEGRITY: XX%". Rappresenta la salute dell'hardware. Il danno è moltiplicato per questo valore. Usa `rep 4` per ripararlo.
*   **Tubi Siluri**: Visibili nell'HUD come "TUBES: <STATO>".
    *   **READY**: I sistemi sono armati e pronti a sparare.
    *   **FIRING...**: Un siluro è attualmente in volo. Nuovi lanci sono inibiti fino all'impatto o all'uscita dal settore.
    *   **LOADING...**: Sequenza di raffreddamento e ricarica post-lancio (circa 5 secondi).
    *   **OFFLINE**: L'integrità dell'hardware è sotto il 50%. Il lancio è impossibile finché il sistema non viene riparato (`rep 5`).

### 💓 Supporto Vitale e Sicurezza dell'Equipaggio
L'HUD visualizza "LIFE SUPPORT: XX.X%", che è direttamente collegato all'integrità dei sistemi vitali della nave.
*   **Inizializzazione**: Ogni missione inizia con il Supporto Vitale al 100%.
*   **Soglia Critica**: Se la percentuale scende sotto il **75%**, l'equipaggio inizierà a subire perdite a causa di fallimenti ambientali (radiazioni, perdita di ossigeno o fluttuazioni di gravità).
*   **Riparazioni di Emergenza**: Mantenere il Supporto Vitale sopra la soglia è la massima priorità. Usa immediatamente `rep 7` se l'integrità è compromessa.
*   **Fallimento della Missione**: Se il numero dell'equipaggio raggiunge lo **zero**, il vascello viene dichiarato perso e la simulazione termina.

**Feedback HUD**: L'allocazione attuale è visibile nel pannello diagnostico in basso a destra come `POWER: E:XX% S:XX% W:XX%`. Il monitoraggio è essenziale per assicurarsi che la nave sia ottimizzata per la fase di missione corrente (Esplorazione vs. Combattimento).

### 📦 Operazioni e Risorse
*   `bor [ID]`: **Squadra d'Abbordaggio**. Invia squadre d'abbordaggio (Dist < 1.0).
    *   Funziona sul **bersaglio attualmente agganciato** se non viene specificato alcun ID.
    *   **Interazione NPC/Relitto**: Ricompense automatiche (Aetherium, Chip, Riparazioni, Superstiti o Prigionieri).
    *   **Interazione Giocatore-Giocatore**: Apre un **Menu Tattico Interattivo** con scelte specifiche:
        *   **Vascelli Alleati**: `1`: Trasferisci Energia, `2`: Ripara Sistema, `3`: Invia Rinforzi Equipaggio.
        *   **Vascelli Ostili**: `1`: Sabotaggio Sistema, `2`: Incursione nella Stiva, `3`: Cattura Ostaggi.
    *   **Selezione**: Rispondi con il numero `1`, `2` o `3` per eseguire l'azione.
    *   **Rischi**: Possibilità di resistenza (30% per i giocatori, più alta per gli NPC) che può causare perdite nella squadra.
*   `dis`: **Smantellamento**. Smantella i relitti nemici per le risorse (Dist < 1.5).
*   `min`: **Estrazione**. Estrae risorse da un pianeta o asteroide in orbita (Dist < 3.1).
    *   **Priorità Selettiva**:
        1.  Se un bersaglio è agganciato (`lock <ID>`), il sistema gli garantisce la priorità assoluta.
        2.  Senza un aggancio, il sistema estrarrà l'oggetto estraibile **assolutamente più vicino**.
    *   **Feedback Radio**:
        *   Asteroidi: `[RADIO] MINING (Comando dell'Alleanza): Estrazione asteroide completata.`
        *   Pianeti: `[RADIO] GEOLOGY (Comando dell'Alleanza): Estrazione planetaria riuscita.`
*   `sco`: **Solar Scooping**. Raccoglie energia da una stella (Dist < 3.1).
*   `har`: **Raccolta Plasma Reserves**. Raccoglie antimateria da un buco nero (Dist < 3.1).
*   `con T A`: **Conversione Risorse**. Converte materie prime in energia o siluri (`T`: tipo di risorsa, `A`: quantità).
    *   `1`: Aetherium -> Energia (x10).
    *   `2`: Neo-Titanium -> Energia (x2).
    *   `3`: Void-Essence -> Siluri (1 ogni 20).
    *   `6`: Gas -> Energia (x5).
    *   `7`: Composite -> Energia (x4).
    *   `8`: **Dark-Matter** -> Energia (x25). [Massima Efficienza]. Minerale radioattivo estremamente raro. Convertilo con `con 8 <amount>` per ricaricare istantaneamente le riserve di energia del Cargo.
*   `load <T> <A>`: **Caricamento Sistemi**. Trasferisce energia o siluri dalla stiva ai sistemi attivi.
    *   `1`: Energia (Reattore Principale). Capacità max: 9.999.999 unità. Permette di convertire l'Plasma Reserves raccolta dai Buchi Neri in energia operativa.
    *   `2`: Siluri (Tubi di Lancio). Capacità max: 1000 unità.

#### 🏗️ Rinforzo dello Scafo (Hull Plating)
*   `hull`: **Rinforzo Scafo**. Usa **100 unità di Composite** per applicare una placcatura rinforzata allo scafo (+500 unità di integrità).
    *   La placcatura in Composite funge da scudo fisico secondario, assorbendo i danni residui che superano gli scudi energetici prima che colpiscano il reattore principale.
    *   Lo stato della placcatura è visibile nell'HUD 3D e tramite il comando `sta`.
*   `inv`: **Inventario**. Mostra il contenuto della stiva di carico, inclusi i materiali grezzi (**Grafene**, **Synaptics**, **Composite**) e i **Prigionieri**.

### 📦 Gestione del Carico e delle Risorse

Space GL distingue tra **Sistemi Attivi**, **Stoccaggio del Carico** e l'**Unità Prigione**. Questo è riflesso nell'HUD come `ENERGY: X (CARGO: Y)`.



*   **Energia/Siluri Attivi**: Queste sono le risorse attualmente disponibili per l'uso immediato.

*   **Riserve del Carico (Stiva)**: Risorse conservate per il rifornimento a lungo raggio.

    *   **Tabella delle Risorse**:

        1. **Aetherium**: Salto Hyperdrive e conversione energetica.

        2. **Neo-Titanium**: Riparazione dello scafo e conversione energetica.

        3. **Void-Essence**: Materiale per le testate dei siluri al plasma (`[WARHEADS]`).

        4. **Grafene**: Lega strutturale avanzata.

        5. **Synaptics**: Chip per riparazioni di sistemi complessi.

                6. **Gas Nebulare**: Raccolto dalle comete, conversione energetica.

                7. **Composite**: Placcatura corazzata dello scafo.

                8. **Dark-Matter**: Raro minerale radioattivo trovato in asteroidi e pianeti specializzati. Usato per tecnologie sperimentali e potenziamento dei sistemi.

*   **Unità Prigione**: Un'unità dedicata alla detenzione del personale nemico catturato durante le operazioni d'abbordaggio. È monitorata in tempo reale nell'HUD vitale accanto al conteggio dell'equipaggio.

*   **Conversione delle Risorse**: Le materie prime devono essere convertite (`con`) in **CARGO Plasma Reserves** o **CARGO Torpedoes** prima di essere caricate nei sistemi attivi.

*   `rep [ID]`: **Riparazione**. Ripara un sistema danneggiato (salute < 100%) ripristinandolo alla piena efficienza. Essenziale per correggere il rumore dei sensori o riattivare le armi offline.
    *   **Costo**: Ogni riparazione consuma **50 Neo-Titanium** e **10 Chip Synaptics**.
    *   **Utilizzo**: Se non viene fornito alcun ID, elenca tutti i 10 sistemi della nave con il loro attuale stato di integrità.
    *   **ID dei Sistemi**: `0`: Hyperdrive, `1`: Impulse, `2`: Sensori, `3`: Transp, `4`: Ion Beam, `5`: Torpedini, `6`: Computer, `7`: Supporto Vitale, `8`: Scudi, `9`: Ausiliari.
*   **Gestione dell'Equipaggio**: 
    *   Il numero iniziale del personale dipende dalla classe della nave (es. 1012 per l'Esploratore, 50 per la Scorta).    *   **Integrità Vitale**: Se il **Supporto Vitale** scende sotto il 75%, l'equipaggio inizierà a subire perdite periodiche.
    *   **Integrità degli Scudi**: Se l'integrità del **Sistema Scudi (ID 8)** è bassa, la ricarica automatica dei 6 quadranti è rallentata.
    *   **Condizione di Fallimento**: Se l'equipaggio raggiunge lo **zero**, la missione termina e la nave è considerata persa.

### 3. Guida ai Comandi Operativi e Crittografia Dello spazio profondo

Il ponte di comando di Space GL opera tramite un'interfaccia a riga di comando (CLI) ad alta precisione. Oltre alla navigazione e al combattimento, il simulatore implementa un sofisticato sistema di **Guerra Elettronica** basato sulla crittografia del mondo reale.

#### 🛰️ Comandi Avanzati di Navigazione e Utilità
*   `nav <H> <M> <W> [F]`: **Navigazione Hyperdrive**. Traccia una rotta Hyperdrive verso coordinate relative. `H`: Heading (0-359), `M`: Mark (-90/+90), `W`: Distanza in quadranti, `F`: Fattore Hyperdrive opzionale (1.0 - 9.9).
*   `imp <H> <M> <S>`: **Motore a Impulso**. Navigazione sub-luce all'interno del settore attuale. `S`: Velocità in percentuale (1-100%). Usa `imp <S>` per regolare solo la velocità.
*   `jum <Q1> <Q2> <Q3>`: **Salto Wormhole**. Genera un tunnel spaziale verso un quadrante distante. Richiede **5000 Energia e 1 Cristallo di Aetherium**.
*   `apr <ID> [DIST]`: **Avvicinamento Automatico**. L'autopilota intercetta l'oggetto specificato alla distanza desiderata (default 2.0). Funziona in tutta la galassia per navi e comete.
*   `cha`: **Inseguimento Bersaglio**. Insegue attivamente il bersaglio attualmente agganciato (`lock`).
*   `rep <ID>`: **Riparazione Sistema**. Avvia le riparazioni su un sottosistema (1: Hyperdrive, 2: Impulse, 3: Sensori, 4: Ion Beam, 5: Siluri, ecc.).
*   `inv`: **Rapporto Inventario**. Elenco dettagliato delle risorse nella stiva (Aetherium, Neo-Titanium, Gas Nebulare, ecc.).
*   `dam`: **Rapporto Danni**. Stato dettagliato dell'integrità dello scafo e dei sistemi.
*   `cal <Q1> <Q2> <Q3>`: **Calcolatore Hyperdrive**. Calcola il vettore verso il centro di un quadrante distante.
*   `cal <Q1> <Q2> <Q3> <X> <Y> <Z>`: **Calcolatore di Precisione**. Calcola il vettore verso coordinate di settore precise `[X, Y, Z]` in un quadrante distante. Fornisce i tempi di arrivo e suggerisce il comando `nav` esatto da copiare.
*   `ical <X> <Y> <Z>`: **Calcolatore d'Impulso (ETA)**. Fornisce un calcolo di navigazione completo per le coordinate di settore precise [0.0 - 10.0], incluso il tempo di viaggio in tempo reale ai livelli di potenza attuali.
*   `who`: **Registro dei Capitani**. Elenca tutti i comandanti attualmente attivi nella galassia, i loro ID di tracciamento e la posizione attuale. Fondamentale per identificare alleati o potenziali predatori prima di entrare in un settore.
*   `sta`: **Rapporto di Stato**. Diagnostica completa dei sistemi, inclusi i livelli di energia, l'integrità dell'hardware e la distribuzione della potenza.
*   `hull`: **Rinforzo in Composite**. Se hai **100 unità di Composite** nella stiva, questo comando applica una placcatura rinforzata allo scafo (+500 HP di scudo fisico), visibile come oro nell'HUD.

#### 🛡️ Crittografia Tattica: "Frequenze" di Comunicazione
In Space GL, la crittografia non riguarda solo la sicurezza: è una **scelta di frequenza tattica**. Ogni algoritmo agisce come una banda di comunicazione separata.

*   **Identità e Firma (Ed25519)**: Ogni pacchetto radio è firmato digitalmente. Se ricevi un messaggio con un tag **`[VERIFIED]`**, hai la certezza matematica che provenga dal capitano dichiarato e non sia stato alterato da sensori nemici o fenomeni spaziali.
*   **Frequenze Crittografiche (`enc <TYPE>`)**:
    *   **AES (`enc aes`)**: Lo standard bilanciato del Comando dell'Alleanza. Sicuro e ottimizzato per l'hardware moderno.
    *   **PQC (`enc pqc`)**: **Crittografia Post-Quantistica (ML-KEM)**. Rappresenta la difesa definitiva contro i computer quantistici dei Swarm o di civiltà temporalmente avanzate. Il protocollo più sicuro disponibile.
    *   **ChaCha (`enc chacha`)**: Ultra-veloce, ideale per comunicazioni rapide in condizioni instabili Dello spazio profondo.
    *   **Camellia (`enc camellia`)**: Protocollo standard dell'Impero Xylari, noto per la sua struttura elegante e la resistenza agli attacchi a forza bruta.
    *   **ARIA (`enc aria`)**: Standard utilizzato dall'Alleanza Korthian per le operazioni di coalizione.
    *   **IDEA / CAST5**: Protocolli spesso usati da gruppi di resistenza (Maquis) o mercenari per evitare il monitoraggio standard del Comando dell'Alleanza.
    *   **OFF (`enc off`)**: Comunicazione in chiaro. Rischiosa, ma utile per chiamate di soccorso universali leggibili da chiunque.

**Implicazione Tattica**: Se una flotta alleata decide di operare sulla "Frequenza ARIA", ogni membro deve impostare `enc aria`. Chi rimane su AES vedrà solo rumore statico (**`<< SIGNAL DISTURBED >>`**), permettendo comunicazioni sicure e segrete anche in settori affollati.

### 📡 Comunicazioni e Varie
*   `rad <MSG>`: Invia un messaggio radio a tutti (Canale aperto).
    *   **Tabella delle Fazioni (@Fac)**:
        | Fazione | Nome Completo | Abbreviato |
        | :--- | :--- | :--- |
        | **Alleanza** | `@Alliance` | `@Fed` |
        | **Korthian** | `@Korthian` | `@Kli` |
        | **Xylari** | `@Xylari` | `@Rom` |
        | **Swarm** | `@Swarm` | `@Bor` |
        | **Vesperian** | `@Vesperian` | `@Car` |
        | **Ascendant** | `@Ascendant` | `@Jem` |
        | **Quarzite** | `@Quarzite` | `@Tho` |
        | **Gilded** | `@Gilded` | `@Fer` |
        | **Fluidic Void** | `@FluidicVoid`| `@8472` |
        | **Saurian / Cryos / Apex** | Nome Completo | - |
*   `rad #ID <MSG>`: Messaggio privato all'ID del giocatore.
*   `psy`: **Guerra Psicologica**. Tenta un bluff (Manovra Corbomite).
*   `axs` / `grd`: Attiva/disattiva le guide visive 3D (Assi / Griglia).
*   `h` (tasto rapido): Nasconde l'HUD per una vista "cinematografica".

La vista 3D non è una semplice finestra grafica ma un'estensione del ponte di comando che fornisce dati telemetrici sovrapposti (Realtà Aumentata) per supportare il processo decisionale del capitano.

#### 🎯 Proiezione Tattica Integrata
Il sistema utilizza algoritmi di proiezione spaziale per ancorare le informazioni direttamente sopra le entità rilevate:
*   **Targeting Tags**: Ogni vascello è identificato con un'etichetta dinamica avanzata.
    *   **Giocatori Alleanza**: Visualizza `Alliance - [Classe] ([Nome Capitano])`.
    *   **Giocatori Alieni**: Visualizza `[Fazione] ([Nome Capitano])`.
    *   **Unità NPC**: Visualizza `[Fazione] [ID]`.
*   **Barre della Salute**: Indicatori cromatici della salute (Verde/Giallo/Rosso) visualizzati sopra ogni nave e stazione, permettendo una valutazione istantanea dello stato del nemico senza consultare i log testuali.
*   **Visual Latching**: Gli effetti di combattimento (Ion Beam, esplosioni) sono sincronizzati temporalmente con la logica del server, fornendo un feedback visivo immediato sull'impatto del colpo.

#### 🧭 Bussola Tattica 3D (`axs`)
Attivando gli assi visivi (`axs`), il simulatore proietta un sistema di riferimento sferico centrato sulla tua nave.

**Nota sulla Convenzione degli Assi**: Il motore grafico utilizza lo standard **OpenGL (Y-Up)**.
*   **X (Rosso)**: Asse trasversale (Sinistra/Destra).
*   **Y (Verde)**: Asse verticale (Alto/Basso). Questo è l'asse di rotazione per l'**Heading**.
*   **Z (Blu)**: Asse longitudinale (Profondità/Movimento in avanti).
*   **Coordinate di Confine**: Al centro di ogni faccia del cubo tattico, sono proiettate le coordinate `[X,Y,Z]` dei quadranti adiacenti.

---

### 🖥️ Architettura del Server e Telemetria GDIS

Al lancio di `stellar_server`, il sistema esegue una diagnostica completa dell'infrastruttura host, visualizzando un pannello di telemetria **GDIS (Library Computer Access and Retrieval System)**.

#### 📊 Dati Monitorati
*   **Infrastruttura Logica**: Identificazione dell'host, versione del kernel Linux e librerie core (**GNU libc**).
*   **Allocazione di Memoria**: 
    *   **RAM Fisica**: Stato della memoria fisica disponibile.
    *   **Segmenti Condivisi (SHM)**: Monitoraggio dei segmenti di memoria condivisa attivi, vitali per l'IPC *Direct Bridge*.
*   **Topologia di Rete**: Elenco delle interfacce attive, indirizzi IP e statistiche sul traffico in tempo reale (**RX/TX**) recuperate dal kernel.
*   **Dinamica Dello spazio profondo**: Carico medio del sistema e numero di task logici attivi.

Questa diagnostica assicura che il server operi in un ambiente nominale prima di aprire i canali di comunicazione Dello spazio profondo sulla porta `3073`.


#### 📟 Telemetria e Monitoraggio HUD
L'interfaccia sullo schermo (Overlay) fornisce un monitoraggio costante dei parametri vitali:
*   **Stato del Reattore e degli Scudi**: Visualizzazione in tempo reale dell'energia disponibile e della potenza media della griglia difensiva.
*   **Monitoraggio del Carico**: Monitoraggio esplicito delle riserve di **CARGO ANTIMATTER** e **CARGO TORPEDOES** per un rifornimento rapido.
*   **Integrità dello Scafo**: Stato fisico dello scafo (0-100%). Se scende a zero, il vascello è perso.
*   **Placcatura dello Scafo**: Indicatore dorato dell'integrità dello scafo rinforzato in Composite (visibile solo se presente).
*   **Coordinate di Settore**: Conversione istantanea dei dati spaziali in coordinate relative `[S1, S2, S3]` (0.0 - 10.0), speculari a quelle usate nei comandi `nav` e `imp`.
*   **Rilevatore di Minacce**: Un contatore dinamico indica il numero di vascelli ostili rilevati dai sensori nel quadrante attuale.
*   **Suite di Diagnostica Deep Space Uplink**: Un pannello diagnostico avanzato (in basso a destra) che monitora la salute del collegamento neurale/dati. Mostra in tempo reale l'Uptime del Link, il **Pulse Jitter**, l'**Integrità del Segnale**, l'efficienza del protocollo e lo stato attivo della crittografia **AES-256-GCM**.

#### 🛠️ Personalizzazione della Vista
Il comandante può configurare la propria interfaccia tramite rapidi comandi CLI:
*   `grd`: Attiva/Disattiva la **Griglia Tattica Galattica**, utile per percepire profondità e distanze.
*   `axs`: Attiva/Disattiva la **Bussola Tattica AR**. Questa bussola olografica è ancorata alla posizione della nave: l'anello dell'azimut (Heading) rimane orientato verso il Nord Galattico (coordinate fisse), mentre l'arco di elevazione (Mark) ruota con il vascello, fornendo un riferimento di navigazione immediato per le manovre di combattimento.
*   `h` (tasto rapido): Nasconde completamente l'HUD per una vista "cinematografica" del settore.
*   **Zoom e Rotazione**: Controllo totale della telecamera tattica tramite mouse o tasti `W/S` e frecce direzionali.

---

## ⚠️ Rapporto Tattico: Minacce e Ostacoli

### Capacità delle Navi NPC
Le navi controllate dal computer (Korthian, Xylari, Swarm, ecc.) operano con protocolli di combattimento standardizzati:
*   **Armamento Primario**: Attualmente, le navi NPC sono equipaggiate esclusivamente con **Banchi Ion Beam**.
*   **Potenza di Fuoco**: I Ion Beam nemici infliggono un danno costante di **10 unità** di energia per colpo (ridotto per bilanciamento).
*   **Portata di Ingaggio**: Le navi ostili apriranno automaticamente il fuoco se un giocatore entra in un raggio di **6.0 unità** (Settore).
*   **Cadenza di Fuoco**: Circa un colpo ogni 5 secondi.
*   **Tattiche**: Le navi NPC non usano siluri al plasma. La loro strategia principale consiste nell'approccio diretto (Chase) o nella fuga se l'energia scende sotto i livelli critici.

### ☄️ Dinamiche dei Siluri al plasma
I siluri (comando `tor`) sono armi simulate fisicamente con alta precisione:
*   **Collisione**: I siluri devono colpire fisicamente il bersaglio (distanza **< 0.8**) per esplodere (raggio aumentato per prevenire il tunneling).
*   **Guida**: Se lanciati con un `lock` attivo, i siluri correggono la loro rotta del **35%** per tick verso il bersaglio, permettendo di colpire navi agili.
*   **Inseguimento Comete**: Puoi usare il comando `cha` (Inseguimento) per seguire le comete lungo la loro orbita galattica, facilitando la raccolta di gas.
*   **Ostacoli**: I corpi celesti come **Stelle, Pianeti e Buchi Neri** sono oggetti fisici solidi. Un siluro che impatta contro di essi verrà assorbito/distrutto senza colpire il bersaglio dietro di essi. Usa il terreno galattico per coprirti!
*   **Basi Stellari**: Anche le basi stellari bloccano i siluri. Attenzione al fuoco amico o incidentale.

### 🌪️ Anomalie Spaziali e Rischi Ambientali
Il quadrante è disseminato di fenomeni naturali rilevabili sia dai sensori che dalla **vista tattica 3D**:
*   **Nebulose (ID 8xxx)**:
    *   **Classi**: Standard, Alta Energia, Materia Oscura, Ionica, Gravimetrica, Temporale.
    *   **Effetto**: Nubi di gas e particelle che interferiscono con i sensori a corto e lungo raggio (rumore telemetrico e distorsione).
    *   **Vista 3D**: Volumi di gas colorati in base alla classe (Viola/Blu per Standard, Giallo/Arancio per Alta Energia, Nero/Viola per Materia Oscura, ecc.).
    *   **Pericolo**: Rimanere all'interno (Distanza < 2.0) causa un drenaggio costante di energia e inibisce la rigenerazione degli scudi.
    *   **Vantaggio**: Fornisce una copertura tattica naturale (occultamento passivo) contro i sensori nemici.
*   **Pulsar (ID 5xxx)**:
    *   **Effetto**: Stelle di neutroni a rotazione rapida che emettono radiazioni letali.
    *   **Vista 3D**: Visibili come nuclei luminosi con fasci di radiazioni rotanti.
    *   **Pericolo**: Avvicinarsi troppo (Distanza < 2.5) danneggia gravemente gli scudi e uccide rapidamente l'equipaggio per avvelenamento da radiazioni.
*   **Comete (ID 6xxx)**:
    *   **Effetto**: Oggetti in movimento veloce che attraversano il settore.
    *   **Vista 3D**: Nuclei ghiacciati con una scia blu di gas e polvere.
    *   **Risorsa**: Avvicinarsi alla coda (< 0.6) permette la raccolta di gas rari.
*   **Campi di Asteroidi (ID 8xxx)**:
    *   **Effetto**: Ammassi di rocce spaziali di varie dimensioni.
    *   **Vista 3D**: Rocce marroni rotanti con forme irregolari.
    *   **Pericolo**: Navigare all'interno ad alta velocità d'impulso (> 0.1) causa danni continui a scudi e motori.
*   **Relitti di Navi (ID 7xxx)**:
    *   **Effetto**: Vascelli del Comando dell'Alleanza o alieni abbandonati.
    *   **Vista 3D**: Scafi scuri e freddi che fluttuano lentamente nello spazio.
    *   **Opportunità**: Possono essere esplorati tramite il comando `bor` (abbordaggio) per recuperare Aetherium, Chip Synaptics o per eseguire riparazioni di emergenza istantanee.
*   **Campi Minati (ID 9xxx)**:
    *   **Effetto**: Zone difensive con mine occultate piazzate da fazioni ostili.
    *   **Vista 3D**: Piccole sfere metalliche chiodate con luce rossa pulsante (visibili solo a distanza < 1.5).
    *   **Pericolo**: La detonazione causa danni massicci agli scudi e all'energia. Usa il comando `scan` per rilevarle prima di entrare nel settore.
*   **Boe di Comunicazione (ID 15xxx)**:
    *   **Effetto**: Nodi della rete del Comando dell'Alleanza per il monitoraggio del settore.
    *   **Vista 3D**: Strutture a traliccio con antenne rotanti e segnali blu pulsanti.
    *   **Vantaggio**: Essere vicino a una boa (**Distanza < 1.2**) potenzia i sensori a lungo raggio (`lrs`), rivelando l'esatta composizione dei quadranti adiacenti (es. `H:1 P:2`) invece di un semplice conteggio totale.
*   **Piattaforme di Difesa (ID 11xxx)**:
    *   **Effetto**: Sentinelle statiche pesantemente armate che proteggono zone strategiche.
    *   **Vista 3D**: Strutture metalliche esagonali con banchi Ion Beam attivi e un nucleo energetico.
    *   **Pericolo**: Estremamente pericolose se approcciate senza scudi al massimo. Sparano automaticamente a bersagli non occultati entro un raggio di 5.0 unità.
*   **Rift Spaziali (ID 12xxx)**:
    *   **Effetto**: Teletrasporti naturali instabili causati da strappi nel tessuto spaziotemporale che ignorano le normali leggi della navigazione Hyperdrive.
    *   **Vista 3D**: Renderizzati come anelli energetici ciano rotanti. Sulla mappa galattica e sui sensori (`srs` o `lrs`), sono contrassegnati dal colore **Ciano** o dalla lettera **R**.
    *   **Rischio/Opportunità**: Entrare in un rift (Distanza < 0.5) proietta istantaneamente la nave in un punto completamente casuale dell'universo (quadrante e settore casuali). Può essere un pericolo fatale (es. territorio Swarm) o l'unica via di fuga rapida durante un attacco critico.
*   **Mostri Spaziali (ID 13xxx)**:
    *   **Entità Cristallina**: Predatore geometrico che insegue le navi e spara raggi di risonanza cristallina.
    *   **Ameba Spaziale**: Forma di vita gigante che drena energia al contatto.
    *   **Pericolo**: Estremamente rari e pericolosi. Richiedono tattiche di gruppo o massima potenza di fuoco.
*   **Tempeste Ioniche**:
    *   **Effetto**: Eventi globali casuali sincronizzati in tempo reale sulla mappa.
    *   **Frequenza**: Alta (media statistica di un evento ogni 5-6 minuti).
    *   **Impatto Tecnico**: Colpire una tempesta **dimezza istantaneamente** la salute dei sensori (ID 2).
    *   **Degradazione Funzionale**: I sensori danneggiati (< 100%) producono "rumore" nei rapporti SRS/LRS (oggetti fantasma, dati mancanti o coordinate imprecise). Sotto il 25%, i sensori diventano quasi inutilizzabili.
    *   **Dettagli Tecnici**: Controllati ogni 1000 tick (33s), probabilità di evento del 20%, con un peso specifico del 50% per le tempeste ioniche.
    *   **Pericolo**: Possono accecare i sensori o spingere violentemente la nave fuori rotta.

## 📡 Dinamiche di Gioco ed Eventi
L'universo di Space GL è animato da una serie di eventi dinamici che richiedono decisioni rapide dal ponte di comando.

#### ⚡ Eventi Casuali del Settore
*   **Ondate Dello spazio profondo**: Fluttuazioni improvvise che possono ricaricare parzialmente le riserve di energia o causare un sovraccarico con conseguente perdita di energia.
*   **Cesoie Spaziali**: Correnti gravitazionali violente che colpiscono la nave durante la navigazione, spingendola fisicamente fuori rotta.
*   **Emergenza Supporto Vitale**: Se il sistema `Life Support` è danneggiato, l'equipaggio inizierà a subire perdite. Questa è una condizione critica che richiede riparazioni urgenti o l'attracco a una base stellare.

#### 🚨 Protocolli Tattici e di Emergenza
*   **Bluff Corbomite**: Il comando `psy` ti permette di trasmettere un falso segnale di minaccia nucleare. Se ha successo, i vascelli nemici entreranno immediatamente in modalità ritirata.
*   **Protocollo di Soccorso di Emergenza**: In caso di distruzione del vascello o collisione fatale, al login successivo il Comando dell'Alleanza avvierà una missione di soccorso automatica, riposizionando la nave in un settore sicuro e ripristinando i sistemi principali all'80% di integrità.
*   **Resistenza all'Abbordaggio**: Le operazioni d'abbordaggio (`bor`) non sono prive di rischi; le squadre possono essere respinte, causando danni interni ai circuiti Synaptics della tua nave.

---

## 🎖️ Registro Storico dei Comandanti

Questa sezione fornisce un riferimento ufficiale ai comandanti più celebrati della galassia, utile per l'ispirazione dei giocatori o per designare vascelli d'élite.

### 🔴 Comandanti delle Potenze Galattiche

#### 1. Impero Korthian
<table>
<tr>
    <td><img src="readme_assets/gpc-korthian.png" alt="Korthian Empire" width="200"/></td>
  </tr>
</table>
*   **Kor**: Il leggendario "Maestro Dahar", pioniere dei primi contatti tattici con l'Alleanza.
*   **Khorak**: Comandante Supremo delle forze Korthian durante la Grande Guerra Galattica.
*   **Dahar**: Cancelliere e veterano della Guerra Civile Korthian.

#### 2. Impero Stellare Xylari
<table>
<tr>
    <td><img src="readme_assets/gpc-xylari.png" alt="Xylari Star Empire" width="200"/></td>
  </tr>
</table>
*   **Valerius**: Comandante di vascelli di classe D'deridex e storico avversario tattico.
*   **Alara**: Comandante operativo e stratega specializzata in operazioni di infiltrazione.
*   **Donatra**: Comandante della *Valdore*, nota per la cooperazione tattica durante la crisi di Shinzon.

#### 3. Collettivo Swarm
<table>
<tr>
    <td><img src="readme_assets/gpc-swarm.png" alt="Swarm Collective" width="200"/></td>
  </tr>
</table>
*   🤖 **Nodo-Alpha 01**: La prima intelligenza dell'alveare a coordinare l'assimilazione tecnologica di interi sistemi stellari.
*   **La Regina**: Nodo di coordinamento centrale del Collettivo.
*   **Unimatrice 01**: Designazione di comando per i vascelli di classe Diamond o i Cubi Tattici.

#### 4. Unione Vesperiana
<table>
<tr>
    <td><img src="readme_assets/gpc-vesperian.png" alt="Korthian Empire" width="200"/></td>
  </tr>
</table>
*   **Gul Dukat**: Leader delle forze d'occupazione e comandante della stazione Terok Nor.
*   **Gul Madred**: Esperto in interrogatori e operazioni di intelligence.
*   **Gul Damar**: Leader della resistenza vesperiana e successore al comando supremo.

#### 5. Ascendente (Ascendant)
<table>
<tr>
    <td><img src="readme_assets/gpc-ascendant.png" alt="Ascendant" width="200"/></td>
  </tr>
</table>
*   **Remata'Klan**: Primo degli Ascendant, simbolo di disciplina e lealtà assoluta.
*   **Ikat'ika**: Comandante delle forze di terra e maestro del combattimento tattico.
*   **Karat'Ulan**: Comandante operativo nel Quadrante Gamma.

#### 6. Matrice Quarzite
<table>
<tr>
    <td><img src="readme_assets/gpc-quarzite-matrix.png" alt="Quarzite Matrix" width="200"/></td>
  </tr>
</table>
*   **Loskene**: Comandante noto per l'impiego della rete energetica Quarzite.
*   **Terev**: Ambasciatore e comandante coinvolto in dispute territoriali.
*   **Sthross**: Comandante di flottiglia esperto in tattiche di confinamento energetico.

#### 7. Legione Sauriana
<table>
<tr>
    <td><img src="readme_assets/gpc-saurian.png" alt="Saurian Legion" width="200"/></td>
  </tr>
</table>
*   **Slar**: Comandante guerriero attivo durante le prime fasi di espansione.
*   **S'Sless**: Capitano incaricato della difesa degli avamposti di frontiera.
*   **Varn**: Comandante della flotta durante le scaramucce nel Quadrante Alfa.

#### 8. Cartello Dorato
<table>
<tr>
    <td><img src="readme_assets/gpc-guilded.png" alt="Gilded Cartel" width="200"/></td>
  </tr>
</table>
*   **DaiMon Bok**: Noto per l'impiego di tecnologie di simulazione e vendette personali.
*   **DaiMon Tog**: Comandante specializzato in acquisizioni tecnologiche forzate.
*   **DaiMon Goss**: Rappresentante tattico durante i negoziati per il controllo del Wormhole.

#### 9. Vuoto Fluidico (Fluidic Void)
<table>
<tr>
    <td><img src="readme_assets/gpc-fluidic.png" alt="Fluidic Void" width="200"/></td>
  </tr>
</table>
*   **Boothby (Impostore)**: Entità dedicata all'infiltrazione e allo studio del comando della Flotta.
*   **Bio-Nave Alpha**: Designazione del coordinatore tattico dei vascelli organici.
*   **Valerie Archer (Impostore)**: Soggetto di infiltrazione per missioni di ricognizione profonda.

#### 10. Enclave Cryos
<table>
<tr>
    <td><img src="readme_assets/gpc-cryos.png" alt="Cryos Enclave" width="200"/></td>
  </tr>
</table>
*   **Thot Pran**: Comandante di alto rango durante l'offensiva nel Quadrante Alfa.
*   **Archon**: Leader operativo durante l'alleanza strategica con gli Ascendant.
*   **Thot Tarek**: Comandante delle forze d'attacco Cryos.

#### 11. Apex
<table>
<tr>
    <td><img src="readme_assets/gpc-apex.png" alt="Apex" width="200"/></td>
  </tr>
</table>
*   **Karr**: Alpha Apex esperto in simulazioni di caccia su larga scala.
*   **Idrin**: Cacciatore veterano e comandante di vascelli preda.
*   **Turanj**: Comandante specializzato nel tracciamento a lungo raggio.

---

## 🎖️ Registro Storico dei Comandanti (Database GDIS)

Il database centrale GDIS preserva le gesta dei comandanti che hanno plasmato i confini dello spazio conosciuto attraverso le tenebre e la luce.

## 🌌 1. Alleanza Stellare (Alliance)
<table>
<tr>
    <td><img src="readme_assets/com-alliance3.png" alt="Alliance Emblem" width="400"/></td>
  </tr>
</table>

---
> **Nota di Comando:** L'Alleanza Stellare non è solo una coalizione militare, ma un ideale di ordine e progresso che si contrappone al caos dei territori di frontiera e all'oscurità dei quadranti inesplorati.
---
### 🏛️ Panoramica Generale
L'**Alleanza Stellare** rappresenta il principale baluardo di stabilità e cooperazione tra le potenze del quadrante. Fondata sui principi della **diplomazia proattiva**, dell'**esplorazione scientifica** e della **difesa collettiva**, l'Alleanza funge da entità coordinatrice tra diverse civiltà per contrastare le minacce sistemiche che mettono a rischio lo spazio conosciuto.

### 🛡️ Pilastri Strategici

* **Dottrina Strategica** A differenza delle potenze espansioniste o collettiviste, l'Alleanza predilige un approccio basato sul **multilateralismo**. La sua forza risiede nella capacità di integrare tattiche e tecnologie eterogenee provenienti da culture diverse sotto un comando unico e coordinato.

* **Eccellenza Diplomatica** È il fulcro della negoziazione galattica, celebre per la sua abilità nel trasformare conflitti secolari in trattati di pace duraturi attraverso il dialogo costante e la mediazione tattica.

* **Capacità Militare** Sebbene orientata alla pace, l'Alleanza mantiene una flotta d'élite altamente specializzata. Eccelle in particolare:
    * Nella difesa di punti di passaggio strategici (*chokepoints*).
    * Nella mappatura di anomalie spaziali complesse.
    * Nella gestione di crisi umanitarie o biologiche su vasta scala.

* **Obiettivi Operativi** La preservazione della libertà di navigazione, la tutela del commercio interstellare e la resistenza organizzata contro le forze di assimilazione tecnologica o di annientamento biologico.

---


<table>
<tr>
    <td><img src="readme_assets/com-niklaus.png" alt="Emblem" width="200"/></td>
    <td><img src="readme_assets/actor-HighAdmiralHyperionNiklaus.png" alt="High Admiral Hyperion Niklaus" width="200"/></td>
  </tr>
</table>

*   🛡️ **Alto Ammiraglio Hyperion Niklaus**: Noto come "Il Muro di Orione", guidò la difesa dell'Aegis durante la prima grande invasione Swarm.
<table>
<tr>
    <td><img src="readme_assets/com-LyraVance.png" alt="Emblem" width="200"/></td>
    <td><img src="readme_assets/actor-CaptainLyraVance.png" alt="Captain Lyra Vance" width="200"/></td>
  </tr>
</table>
*   ⚓ **Capitano Lyra Vance**: La leggendaria esploratrice che mappò il Ponte di Einstein-Rosen verso il Quadrante Delta usando un vascello di classe Scout.
<table>
<tr>
    <td><img src="readme_assets/com-LeandrosThorne.png" alt="Emblem" width="200"/></td>
    <td><img src="readme_assets/actor-CommanderLeandrosThorne.png" alt="Commander Leandros Thorne" width="200"/></td>
  </tr>
</table>
*   📜 **Comandante Leandros Thorne**: Un raffinato diplomatico e tattico, famoso per il Trattato di Aetherium che pose fine alla guerra centenaria con i Korthian.

#### ⚔️ 2. Impero Korthian
*   🩸 **Signore della Guerra Khorak**: Il tattico più brutale dell'impero, famoso per la sua dottrina del "Fuoco Perpetuo" e la conquista del Settore Nero.
*   🗡️ **Generale Valkar**: Un leggendario comandante che unificò le casate in guerra sotto un unico vessillo di conquista galattica.

#### 🎭 3. Impero Stellare Xylari
*   🐍 **Gran Pretore Nyx**: Maestro di furtività e sabotaggio, svanì per dieci anni prima di riemergere con una flotta fantasma nel cuore del territorio nemico.
*   👁️ **Inquisitore Malakor**: Il primo a utilizzare le frequenze di crittografia Camellia per manipolare i flussi di dati dei sensori nemici.

#### 🕸️ 4. Collettivo Swarm
*   🤖 **Nodo-Alpha 01**: La prima intelligenza dell'alveare a coordinare l'assimilazione tecnologica di interi sistemi stellari.
*   🔗 **Unity Prime**: Un'entità biomeccanica incaricata di ottimizzare il consumo di massa stellare nelle nebulose energetiche.

#### 🏛️ 5. Unione Vesperiana
*   📐 **Legato Thrax**: Architetto della difesa galattica, noto per aver trasformato semplici asteroidi in fortezze inespugnabili.

#### 🔮 6. L'Ascendenza (The Ascendancy)
*   🛐 **Primo Arconte Voth**: La guida spirituale e militare che guidò la sua flotta "Ascendente" attraverso il Grande Vuoto.

#### 💎 7. Matrice Quarzite
*   💠 **Rifrazione Zero**: Un'entità cristallina pura capace di calcolare rotte iperspaziali a una velocità superiore a qualsiasi computer biologico.

#### 💎 8. Cartello Dorato
*   📈 **Barone Silas**: Il magnate che monopolizzò il commercio di Void-Essence e Aetherium attraverso tre quadranti.

#### ❄️ 9. Enclave Cryos
*   🧊 **Custode Boreas**: Governatore delle distese ghiacciate, esperto in tattiche di guerriglia termica e soppressione del segnale.

#### 🎯 10. Apex Stalker
*   🏹 **Alpha Hunter Kael**: Noto come "Il Fantasma del Settore Zero", rinomato per non aver mai mancato un bersaglio con i suoi siluri al plasma a guida manuale.

---


### 🔵 Profili Operativi per Classe di Vascello (Alleanza)

In Space GL, la scelta della classe del vascello definisce il profilo operativo del Comandante. Di seguito sono riportati i riferimenti per le classi principali:

#### 🏛️ Classe Legacy (Incrociatore Pesante)
Il simbolo dell'esplorazione dell'Alleanza. Un vascello equilibrato, versatile e robusto.
*   **Comandante di Riferimento**: **Hyperion Niklaus**. La sua leadership sull'Aegis originale ha definito gli standard tattici dell'accademia.

#### 🛡️ Classe Esploratore (Nave Ammiraglia)
Progettata per missioni di lunga durata e primo contatto. Dispone dei sistemi GDIS più avanzati.
*   **Comandante di Riferimento**: **Lyra Vance**. Eccelleva nell'uso dei sensori a lungo raggio per evitare conflitti non necessari.

#### ⚔️ Classe Nave Ammiraglia (Incrociatore Tattico)
L'ultima espressione della potenza di fuoco dell'Alleanza, equipaggiata con pesanti banchi Ion Beam.
*   **Comandante di Riferimento**: **Leandros Thorne**. Famoso per l'uso coordinato di scudi localizzati e salve di siluri al plasma.

#### 🔭 Nave Scientifica (Esploratore Scientifico)
Un vascello specializzato nell'analisi di anomalie spaziali e nella raccolta di Aetherium.
*   **Comandante di Riferimento**: **Inquisitore Malakor** (Acquisito). Sebbene Xylari, le sue teorie sulla risonanza spaziale sono studiate in ogni missione scientifica.

#### 🛠️ Altre Classi Operative
L'Alleanza impiega anche vascelli specializzati come la classe **Carrier** (coordinamento droni) e **Tactical Cruiser** (difesa perimetrale), ciascuno ottimizzato per scenari di crisi specifici.

---

### 🛰️ Nomenclatura Navale dell'Alleanza (Standard GDIS)

Per facilitare il coordinamento tattico, il sistema GDIS adotta una nomenclatura standardizzata per i componenti dei vascelli dell'Alleanza, qui illustrata sulla configurazione "Monoblocco" della classe Legacy:

1.  **Scafo Primario (Modulo di Comando)**: Il corpo discoidale primario, che ospita i ponti di comando, gli alloggi e i laboratori scientifici.
2.  **Hub Tattico (Modulo Ponte)**: La cupola superiore rinforzata, il centro di calcolo per il puntamento delle armi e la gestione della flotta.
3.  **Sezione Ingegneria (Scafo Secondario)**: La sezione oblunga posteriore integrata, progettata per ospitare il reattore al plasma e i serbatoi di Aetherium.
4.  **Array Deflettore Principale (Sfera Posteriore)**: Una sfera risonante distanziata dal corpo principale, usata per la deflessione delle particelle e la stabilizzazione del flusso Hyperdrive.
5.  **Anelli di Risonanza Energetica**: Una serie di **3 anelli a induzione magnetica rotanti** attorno al deflettore di coda, responsabili della coerenza del campo FTL.
6.  **Piloni Strutturali (Bracci di Supporto)**: Bracci di supporto affusolati a forma di ellissoide che collegano rigidamente la sezione ingegneria alle unità di propulsione.
7.  **Gondole Hyperdrive (Unità FTL)**: Gondole laterali gemelle, generatori primari della bolla Hyperdrive necessari per il volo spaziale superluminale.

Questa architettura allungata, priva di connessioni sottili ("Collo"), rappresenta l'evoluzione tecnologica dell'Alleanza verso vascelli più robusti e resistenti agli impatti cinetici.

---

## 💾 Persistenza e Continuità
L'architettura di Space GL è progettata per supportare una galassia persistente e dinamica. Ogni azione, dalla scoperta di un nuovo sistema planetario al caricamento della Stiva di Carico, è preservata tramite un sistema di archiviazione binaria a basso livello.

#### 🗄️ Il Database Galattico (`galaxy.dat`)
Il file `galaxy.dat` costituisce la memoria storica del simulatore. Utilizza una struttura di **Serializzazione Diretta** della RAM del server:
*   **Matrice Master della Galassia**: Una griglia tridimensionale 10x10x10 che memorizza la densità di massa e la composizione di ogni quadrante (codifica BPNBS).
*   **Registri delle Entità**: Un dump completo degli array globali (`npcs`, `stars_data`, `planets`, `bases`), che preserva le coordinate relative, i livelli di energia e i timer di cooldown.
*   **Integrità dei Dati**: Implementa un rigido controllo di versione (`GALAXY_VERSION`). Se il server rileva un file generato con parametri strutturali diversi, invalida il caricamento per prevenire la corruzione della memoria, rigenerando un universo coerente.

#### 🔄 Pipeline di Sincronizzazione (Salvataggio Automatico)
La continuità è garantita da un loop di sincronizzazione asincrona:
*   **Flush Periodico**: Ogni 60 secondi, il thread logico avvia una procedura di salvataggio.
*   **Sicurezza dei Thread**: Durante l'operazione di I/O su disco, il sistema acquisisce il `game_mutex`. Questo assicura che il database salvato sia uno **snapshot atomico** e una rappresentazione coerente dell'intero universo in quel preciso momento.

#### 🆔 Ripristino dell'Identità e del Profilo
Il sistema di continuità per i giocatori si basa sull'**Identità Persistente**:
*   **Riconoscimento**: Inserendo lo stesso nome del capitano usato in precedenza, il server interroga il database dei giocatori attivi e storici.
*   **Recupero della Sessione**: Le coordinate globali, l'inventario strategico e gli stati del sistema vengono ripristinati istantaneamente.

#### 🆘 Protocollo EMERGENCY RESCUE
In caso di distruzione del vascello, al login successivo, il Comando dell'Alleanza attiva un protocollo di recupero: ripristino dell'80% del sistema, rifornimento di emergenza e rilocazione in un settore sicuro.

Questa architettura garantisce che Space GL non sia solo una sessione di gioco, ma una vera carriera spaziale in evoluzione.

## 🔐 Crittografia Dello spazio profondo: Approfondimento Tattico

Space GL implementa una suite crittografica a più livelli che trasforma la sicurezza delle comunicazioni in una vera meccanica di gioco tattica. Ogni algoritmo rappresenta una diversa "frequenza" operativa.

### 📡 Protocolli di Trasmissione e Autenticazione

Oltre alla scelta dell'algoritmo, il sistema GDIS utilizza protocolli avanzati per garantire che ogni ordine provenga dal legittimo comandante:

*   **Handshake Iniziale (Offuscamento XOR)**: All'aggancio, il client e il server negoziano una **Chiave di Sessione** unica a 256 bit. Questo scambio avviene tramite un protocollo di offuscamento XOR basato sulla **Master Key** del settore (`SPACEGL_KEY`), assicurando che nessun pacchetto sia leggibile senza l'autorizzazione iniziale.
*   **Firme Digitali Ed25519**: Ogni pacchetto inviato via radio (`rad`) è firmato digitalmente. Il ricevitore verifica istantaneamente l'autenticità usando curve ellittiche. I messaggi autentici sono contrassegnati con **`[VERIFIED]`** in verde.
*   **Integrazione della Frequenza Rotante**: Il Vettore di Inizializzazione (IV) di ogni messaggio viene modificato dinamicamente in base al `frame_id` del server. Questo rende il sistema immune agli *Attacchi di Replay*: un messaggio registrato un secondo fa sarà illeggibile quello successivo.

### ⚛️ Suite di Algoritmi (Frequenze Operative)

Il comando `enc <ALGO>` permette di sintonizzare i sistemi di bordo su uno dei seguenti standard:

#### 1. ML-KEM-1024 (Kyber) - `enc pqc`
*   **Descrizione**: Crittografia Post-Quantistica basata su reticoli.
*   **Uso Tattico**: Il culmine della segretezza galattica. Usato dalla **Sezione Ombra** per le comunicazioni che devono rimanere protette anche contro futuri attacchi da computer quantistici da parte degli Swarm o di civiltà temporalmente avanzate. Invulnerabile alla tecnologia convenzionale.

#### 2. AES-256-GCM - `enc aes`
*   **Descrizione**: Advanced Encryption Standard con modalità Galois/Counter.
*   **Uso Tattico**: Lo standard ufficiale del **Comando dell'Alleanza**. Offre il miglior equilibrio tra sicurezza estrema e velocità, grazie all'accelerazione hardware dei processori Synaptics. Include l'autenticazione dei messaggi (AEAD).

#### 3. ChaCha20-Poly1305 - `enc chacha`
*   **Descrizione**: Cifrario a flusso moderno abbinato a un autenticatore di messaggi.
*   **Uso Tattico**: Preferito dai vascelli di classe **Scout** ed **Escort**. Estremamente veloce in ambienti dove la potenza di calcolo è limitata, garantendo una latenza minima nei collegamenti tattici.

#### 4. ARIA-256-GCM - `enc aria`
*   **Descrizione**: Standard di crittografia a blocchi sudcoreano certificato.
*   **Uso Tattico**: Rappresenta la frequenza di coalizione tra l'**Alleanza** e l'**Impero Korthian**. Usato per operazioni congiunte su larga scala.

#### 5. Camellia-256-CTR - `enc camellia`
*   **Descrizione**: Cifrario a blocchi ad alta efficienza di origine terrestre (Giapponese).
*   **Uso Tattico**: Lo standard imperiale dell'**Impero Stellare Xylari**. Noto per la sua eleganza matematica e la resistenza ai tentativi di decrittazione a forza bruta da parte degli infiltrati.

#### 6. IDEA-CBC - `enc idea`
*   **Descrizione**: International Data Encryption Algorithm.
*   **Uso Tattico**: La frequenza della **Resistenza** e dei gruppi indipendenti. Resiliente e difficile da analizzare per i computer centralizzati delle grandi potenze.

#### 7. Blowfish-CBC - `enc bf`
*   **Descrizione**: Progettato per essere veloce e compatto.
*   **Uso Tattico**: Protocollo commerciale standard del **Cartello Dorato**. Usato per proteggere le transazioni di Aetherium e i manifesti di carico.

#### 8. CAST5-CBC - `enc cast`
*   **Descrizione**: Algoritmo a chiave variabile (fino a 128 bit).
*   **Uso Tattico**: Usato nelle regioni di confine per le comunicazioni civili e del governo locale.

#### 9. Triple DES (3DES) - `enc 3des`
*   **Descrizione**: Tripla applicazione del Data Encryption Standard.
*   **Uso Tattico**: Frequenza "Legacy". Usata per accedere agli archivi storici e comunicare con antiche stazioni spaziali automatizzate.

#### 10. SEED-CBC - `enc seed`
*   **Descrizione**: Cifrario a blocchi a 128 bit.
*   **Uso Tattico**: Usato principalmente nei protocolli industriali e logistici pesanti dell'Unione Vesperiana.

#### 11. RC4 Stream - `enc rc4`
*   **Descrizione**: Storico cifrario a flusso.
*   **Uso Tattico**: Sebbene considerato insicuro per i segreti di stato, viene utilizzato per i collegamenti telemetrici grezzi a bassissima latenza dove la velocità è l'unica priorità.

#### 12. DES-CBC - `enc des`
*   **Descrizione**: L'originale standard terrestre degli anni '70.
*   **Uso Tattico**: Mappato sui **segnali pre-Hyperdrive**. Necessario per decrittare le comunicazioni provenienti da antiche sonde dormienti o segnali da civiltà nelle prime fasi tecnologiche.

---

## 🛠️ Specifiche Tecniche e Quick Start

### ⌨️ Controlli da Tastiera del Visualizzatore 3D
L'interazione con `spacegl_3dview` è gestita tramite i seguenti input diretti:
*   **Tasti Freccia**: Ruotano la telecamera (Beccheggio / Imbardata).
*   **Tasti W / S**: Zoom In / Zoom Out precisi.
*   **Tasto H**: Commuta l'HUD (Nasconde/Mostra l'overlay tattico).
*   **Tasto ESC**: Chiude in sicurezza il Visualizzatore 3D.

### 🚢 Estetica delle classi di navi visuali
Ogni classe di vascello presenta elementi di design 3D unici:
*   **Classe Esploratore**: Caratterizzata da un disco di comando ad alto dettaglio con sonde sensoriali multi-spettrali rotanti.
*   **Incrociatore Pesante**: Design robusto con un massiccio scafo secondario e un disco deflettore ciano ad alta intensità.
*   **Navi Occultate**: Quando l'occultamento è attivo, la nave è sostituita da un effetto **Blue Glowing Wireframe**, che rappresenta visivamente la curvatura della luce attorno allo scafo.

### 🔒 Sicurezza e Integrità dei Dati
Space GL implementa la sicurezza di livello enterprise per la sincronizzazione dello stato galattico:
*   **Firme HMAC-SHA256**: I file dei dati della galassia (`galaxy.dat`) e gli aggiornamenti di rete sono firmati per garantire zero manomissioni durante il transito o l'archiviazione.
*   **HUD Crittografico**: Visualizzazione in tempo reale dei flag di crittografia, dello stato della firma e dei parametri del protocollo attivo direttamente nell'interfaccia tattica.

### ⚙️ Requisiti di Sistema e Dipendenze
Per compilare ed eseguire la suite StarTrek Ultra, assicurati che siano installate le seguenti librerie:
*   **FreeGLUT / OpenGL**: Motore di rendering principale e gestione delle finestre.
*   **GLEW**: OpenGL Extension Wrangler per il supporto avanzato agli shader.
*   **OpenSSL**: Richiesto per la suite crittografica completa (AES, HMAC, ecc.).
*   **POSIX Threads & RT**: Gestiti tramite `lpthread` e `lrt` per la memoria condivisa e la sincronizzazione.

### ⚡ Architettura IPC a Latenza Zero
L'estrema reattività del sistema è ottenuta attraverso un'architettura **Zero-Copy Shared Memory** (`/dev/shm`). Il client binario e il motore 3D comunicano alle velocità della RAM, assicurando che ogni comando impartito nella console risulti in una reazione visiva istantanea senza lag indotto dalla rete sulla macchina locale.

---
*SPACE GL - 3D LOGIC ENGINE. Sviluppato con eccellenza tecnica da Nicola Taibi. "Per Tenebras, Lumen"*
