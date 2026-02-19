# Space GL: 3D Multi-User Client-Server Edition
## Un gioco di esplorazione e combattimento spaziale
## "Per Tenebras, Lumen" ("Attraverso le tenebre, la luce")
### Website: https://github.com/nicolataibi/spacegl
### Authors: Nicola Taibi, Supported by Google Gemini
### Copyright (C) 2026 Nicola Taibi - Licensed under GPL-3.0-or-later
#### **Licenza: CC BY 4.0 (Attribuzione)**
#### Tutte le immagini dei personaggi e le interfacce grafiche presenti in questa collezione sono rilasciate sotto la licenza [Creative Commons Attribution 4.0 International](https://creativecommons.org/licenses/by/4.0/deed.it). 
#### **Cosa puoi fare:** * Sei libero di condividere, copiare, distribuire e adattare il materiale per qualsiasi scopo, anche commerciale. 
#### **Condizioni:** * È necessario fornire i crediti appropriati citando **Nicola Taibi** e fornendo un link alla licenza.
#### *“Questa collezione presenta asset generati dall'intelligenza artificiale, curati e ideati tramite prompt da Nicola Taibi, generati da Gemini (Google IA)”*
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

## 🚀 Novità Versione 2.3 (Deep Space Expansion)

L'aggiornamento 2.3 trasforma radicalmente la scala e la precisione del simulatore:
*   **Scala Galattica 1600x**: Ogni quadrante (40x40x40) è ora composto da una matrice di **40x40 settori**, portando il volume totale a 1600 unità per lato.
*   **Precisione al Centesimo**: Tutti i calcoli di navigazione, i sensori e i sugerimenti del computer (`cal`, `ical`) operano ora con precisione millimetrica (**%.2f**).
*   **ETA HUD**: Il visore 3D visualizza ora il tempo stimato di arrivo in secondi (in giallo) durante il volo verso una destinazione fissata.
*   **Ricalibrazione Fisica**: La velocità Hyperdrive è stata sincronizzata per permettere l'attraversamento della diagonale galattica in **40 secondi** costanti al Fattore 9.9.
*   **Tactical Cube 40x**: Il cubo del quadrante nel visore 3D è stato scalato a 40x40x40 unità per una navigazione più spaziosa e realistica.


---

## 🚀 Novità Versione 2.3 (Deep Space Expansion)

L'aggiornamento 2.3 trasforma radicalmente la scala e la precisione del simulatore:
*   **Scala Galattica 1600x**: Ogni quadrante (40x40x40) è ora composto da una matrice di **40x40 settori**, portando il volume totale a 1600 unità per lato.
*   **Precisione al Centesimo**: Tutti i calcoli di navigazione, i sensori e le sugerimenti del computer (`cal`, `ical`) operano ora con precisione millimetrica (**%.2f**).
*   **ETA HUD**: Il visore 3D visualizza ora il tempo stimato di arrivo in secondi (in giallo) durante il volo verso una destinazione fissata.
*   **Ricalibrazione Fisica**: La velocità Hyperdrive è stata sincronizzata per permettere l'attraversamento della diagonale galattica in **40 secondi** costanti al Fattore 9.9.
*   **Tactical Cube 40x**: Il cubo del quadrante nel visore 3D è stato scalato a 40x40x40 unità per una navigazione più spaziosa e realistica.

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

### 5. Strumenti di Diagnostica (SpaceGL Viewer)
Per monitorare la galassia in tempo reale o ispezionare il file di salvataggio `galaxy.dat`, è disponibile uno strumento di amministrazione dedicato:
```bash
# Esempi di utilizzo
./spacegl_viewer stats          # Statistiche globali e conteggio fazioni
./spacegl_viewer master         # Stato vitale della nave (Energia, Scudi, Inventario)
./spacegl_viewer list 10 1 39   # Elenco oggetti e sonde in un quadrante specifico
./spacegl_viewer search "Nick"  # Trova la posizione di un giocatore o NPC
./spacegl_viewer report         # Genera un'anagrafica completa di tutti gli oggetti (>14.000)
```
Il viewer esegue una diagnostica automatica dell'allineamento binario all'avvio per garantire l'integrità dei dati letti.

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

## 🌌 La Galassia Persistente

Space GL offre un universo vasto e densamente popolato che persiste anche dopo il riavvio del server.

### 1. Scala e Popolazione
La galassia è un cubo **40x40x40** che contiene **64.000 quadranti unici**. Ogni quadrante è ulteriormente suddiviso in una matrice di **40x40x40 settori (unità)**, creando un sistema di coordinate assolute che spazia da **0.0 a 1600.0**.
*   **Fazioni NPC:** Ognuna delle 11 fazioni aliene (Korthian, Swarm, Xylari, etc.) mantiene una flotta permanente composta da **70 a 100 vascelli unici** sempre attivi.
*   **Distribuzione Omogenea:** Corpi celesti, basi stellari e anomalie sono distribuiti proceduralmente nell'intero volume di 64.000 quadranti per garantire un'esperienza di esplorazione bilanciata.
*   **L'Eredità dell'Alleanza**: Sparse tra le stelle si trovano da **70 a 100 relitti storici (derelicts)** per OGNI classe di nave dell'Alleanza. Inoltre, ogni vascello NPC distrutto in combattimento genera ora un **relitto permanente** nel settore, offrendo un ricco scenario per il recupero e l'esplorazione.
*   **Relitti Alieni**: La galassia ospita relitti pre-generati per tutte le fazioni aliene, offrendo opportunità di recupero tecnologico fin dall'inizio della missione.
*   **Diversità Celestiale**:
    *   **Stelle**: Classificate in 7 tipi spettrali: **O (Blu)**, **B (Bianca)**, **A (Bianca)**, **F (Gialla)**, **G (Gialla)**, **K (Arancio)** e **M (Rossa)**.
    *   **Pulsar**: Stelle di neutroni categorizzate in 3 classi scientifiche: **Rotation-Powered**, **Accretion-Powered** e **Magnetar**.
    *   **Nebulose**: Categorizzate in 6 classi tattiche: **Standard**, **Alta Energia**, **Materia Oscura**, **Ionica**, **Gravimetrica** e **Temporale**.
    *   **Minacce Classe-Omega**: Monitoraggio specifico di entità uniche come l'**Entità Cristallina** e l'**Ameba Spaziale**.
*   **Identificazione Univoca**: Ogni vascello nella galassia, sia esso attivo o un relitto, possiede un **nome proprio** estratto da database storici specifici per fazione (es. *IKS Bortas* per i Korthian, *Enterprise* per l'Alleanza). Le etichette generiche "(OTHER)" sono state completamente eliminate dai sensori.

### 2. Sopravvivenza ed Energia (Life Support)
Il realismo della simulazione è garantito da un sistema di consumo energetico dinamico che non si ferma mai:
*   **Life Support (Base Drain)**: La nave consuma costantemente **1 unità di energia per tick** (~30/sec) per mantenere i sistemi vitali.
*   **Allarme Rosso**: L'energizzazione dei sistemi tattici triplica il consumo di base (**3 unità/tick**).
*   **Docking Safe Mode**: Il consumo energetico è completamente sospeso quando la nave è collegata a una Base Stellare (alimentazione esterna).
*   **Emergenza Energia**: Se le riserve scendono a zero, il **Supporto Vitale** inizia a degradarsi dello **0.1% per tick**. Al raggiungimento dello **0%**, si verificheranno **vittime tra l'equipaggio** (1 membro al secondo). Ripristinare l'energia ricaricherà automaticamente il supporto vitale.

### 3. Navigazione Avanzata (Standard GDIS)
Il sistema di navigazione è stato riprogettato per garantire precisione matematica e fluidità visiva:
*   **Coordinate Galattiche Assolute:** Tutti i calcoli di movimento e distanza utilizzano una scala standardizzata **0.0 - 1600.0 assoluta**. Questo garantisce puntamenti coerenti e tracciamento dei siluri affidabile anche quando si attraversano i confini dei quadranti.
*   **Navigazione di Precisione (`nav`):** Il comando `nav` ora integra un sistema di blocco della destinazione. Una volta raggiunte le coordinate `target_gx/gy/gz` calcolate, la nave disattiverà automaticamente i motori e uscirà dall'Hyperdrive nella posizione precisa richiesta.
*   **Ricalibrazione Hyperdrive (Velocità Costante):** Il sistema di propulsione è stato calibrato per transiti ad altissima velocità. **Il Fattore 9.9 percorre l'intera diagonale della galassia (circa 2771 unità) in esattamente 40 secondi.** La velocità è perfettamente costante e indipendente dalla potenza dei motori o dall'integrità per garantire tempi di arrivo corrispondenti alle stime del comando `cal`.
*   **Modello Energetico e Danni:** 
    *   **Drenaggio Lineare**: Il consumo energetico dell'Hyperdrive scala linearmente con la velocità.
    *   **Penalità Integrità**: I sistemi di propulsione danneggiati (Hyperdrive/Impulse) subiscono un aumento dello spreco energetico (dissipazione di calore). Il consumo è inversamente proporzionale all'integrità del sistema.
*   **Autopilota Fluido (LERP Tracking):** Il comando `apr` (approach) non esegue più uno scatto istantaneo dell'orientamento. Utilizza invece l'**Interpolazione Lineare (LERP)** per allineare dolcemente la prua (heading) e il mark della nave con il bersaglio, prevenendo rotazioni erratiche e offrendo un'esperienza di volo cinematografica.
*   **Limiti Galattici:** I confini della galassia sono applicati rigidamente a **[0.05, 1599.95]**. Le navi che tentano di uscire dalla galassia attiveranno automaticamente i freni di emergenza e invertiranno la rotta (virata di 180°) per rimanere nello spazio navigabile.

### 3. Revisione del Combattimento Tattico
Il combattimento presenta ora un modello di danno sofisticato, con tracciamento degli ordigni migliorato e gestione dinamica delle difese:
*   **Tracciamento Assoluto Ordigni:** I siluri si muovono ora utilizzando le **Coordinate Galattiche Assolute**. Questo permette a un siluro lanciato in un quadrante di colpire con successo un bersaglio che si è spostato in un settore adiacente, eliminando i "mancamenti fantasma" ai confini.
*   **Homing Migliorato:** Il sistema di autoguida dei siluri è stato potenziato (fattore di correzione 45%), affidandosi alla salute dei **Sensori (ID 2)** per la precisione.
*   **Scaling della Precisione:** Il danno dei siluri varia in base all'accuratezza dell'impatto. I colpi diretti (<0.2 unità) ricevono un **bonus del 1.2x**, mentre i colpi di striscio (0.5-0.8 unità) sono ridotti allo **0.7x**.
*   **Resistenza di Fazione:** Le tecnologie degli scafi alieni reagiscono diversamente ai siluri dell'Alleanza. Le **Bio-corazze (Swarm, Species 8472)** riducono il danno a **0.6x**, mentre gli scafi fragili commerciali o da esplorazione (**Gilded, Gorn**) subiscono danni aumentati a **1.4x**.
*   **Difesa a Strati e Assorbimento Scudi:**
    *   **Assorbimento Direzionale**: I siluri colpiscono ora settori specifici degli scudi (**Frontale, Posteriore, Superiore, Inferiore, Sinistro, Destro**) in base all'angolo di impatto relativo.
    *   **Priorità Difensiva**: Il danno viene assorbito prioritariamente dallo scudo del settore colpito. Solo se lo scudo è esaurito o insufficiente, il danno residuo viene applicato al **Plating** (corazza composita) e infine allo **Hull** (scafo).
    *   **Drenaggio Energetico**: Anche se gli scudi reggono l'impatto, lo stress cinetico causa un drenaggio minore delle riserve energetiche della nave.
*   **Gestione Intelligente del Lock Target**:
    *   **Rilascio Automatico**: Per garantire la coerenza tattica, il sistema di puntamento (`lock`) viene disattivato automaticamente se il bersaglio viene distrutto, se esce dal quadrante attuale o se la nave del giocatore cambia settore.
    *   **Validazione Continua**: Il computer di bordo monitora costantemente la validità del bersaglio ad ogni tick logico.
*   **Danni Sistemici ai Motori:** Ogni impatto andato a segno infligge dal **10% al 20% di danno permanente** ai motori dell'NPC, causandone la perdita di velocità e manovrabilità durante il corso della battaglia.

### 4. Ottimizzazione Prestazioni e Strutturale (Risoluzione Lag)
Per mantenere un tasso logico di 30 TPS (Tick Per Secondo) costante gestendo un universo massiccio di 64.000 quadranti, l'engine ha subito un refactoring strutturale focalizzato sulla rimozione di tre colli di bottiglia critici:

#### 🧠 A. Dirty Quadrant Indexing (Tecnica "Sparse Reset")
*   **Il Problema**: In precedenza, il server eseguiva un `memset` sull'intero indice spaziale da 275MB e iterava attraverso tutti i 64.000 quadranti ad ogni singolo tick per cancellare i dati obsoleti. Questo consumava una larghezza di banda di memoria e tempo di CPU massicci.
*   **La Soluzione**: Abbiamo implementato un sistema di tracciamento tramite **Dirty List**. 
    *   Solo i quadranti che contengono oggetti dinamici (NPC, Giocatori, Comete) vengono contrassegnati come "sporchi" (dirty).
    *   All'inizio di ogni tick, il loop di reset visita *solo* gli specifici quadranti memorizzati nella lista (tipicamente ~2.000 celle) invece di tutti i 64.000.
    *   **Impatto**: Ridotto il sovraccarico dell'indicizzazione spaziale del **95%**, liberando risorse CPU cruciali per l'IA e la logica di combattimento.

#### 💾 B. I/O Asincrono Non-Bloccante (Background Saving)
*   **Il Problema**: La funzione `save_galaxy()` era sincrona. Ogni 60 secondi, l'intero motore di gioco si "congelava" per diversi millisecondi durante la scrittura del file `galaxy.dat` su disco, causando scatti evidenti o "blocchi di lag".
*   **La Soluzione**: Abbiamo spostato la logica di persistenza in un **thread in background distaccato**.
    *   Il thread logico principale esegue un `memcpy` quasi istantaneo dello stato core in un buffer protetto.
    *   Un thread secondario (`save_thread`) gestisce l'I/O su disco pesante in modo indipendente.
    *   Un flag `atomic_bool` impedisce operazioni di salvataggio sovrapposte se il disco è lento.
    *   **Impatto**: **Latenza di salvataggio zero**. Il loop logico continua a 30Hz perfetti indipendentemente dalle prestazioni del disco.

#### 📡 C. Disaccoppiamento Griglia LRS
*   **Il Problema**: La generazione della griglia globale codificata BPNBS (usata per i Sensori a Lungo Raggio) comporta un triplo loop annidato su 64.000 quadranti. Farlo 30 volte al secondo era ridondante.
*   **La Soluzione**: Abbiamo disaccoppiato la generazione della griglia sensoriale dal tick della fisica.
    *   La griglia strategica viene ora aggiornata solo **una volta al secondo** (ogni 30 tick).
    *   Poiché l'LRS è usato per la pianificazione a lungo raggio, una frequenza di aggiornamento di 1 secondo fornisce una consapevolezza tattica perfetta senza il costo computazionale inutile.
    *   **Impatto**: Eliminato il compito computazionale più oneroso da 29 su 30 frame logici.

#### 📊 Benchmark di Ottimizzazione (Prima vs. Dopo)
| Metrica | Modello Brute-Force | Modello Ottimizzato (v2.1) | Miglioramento |
| :--- | :--- | :--- | :--- |
| **Loop Reset Griglia** | 64.000 iterazioni | ~2.500 iterazioni | **25x più veloce** |
| **Scrittura Memoria (Tick)**| 275 MB (memset) | ~150 KB (selettiva) | **1.800x più efficiente** |
| **Latenza Salvataggio** | ~50-200 ms (Stop-the-world) | < 1 ms (Copia asincrona) | **Fluidità infinita** |
| **Calcolo Griglia LRS** | 1.920.000/sec | 64.000/sec | **Riduzione di 30x** |

#### ⚡ D. Sistema Energetico a 64-bit e Logica di Sicurezza (v2.2)
*   **Il Problema**: Il precedente modello a 32-bit (`int`) limitava l'energia a circa 2 miliardi di unità, insufficienti per simulazioni di grandi flotte o persistenza a lungo termine. Inoltre, le sottrazioni dirette erano vulnerabili all'underflow.
*   **La Soluzione**: Abbiamo rifattorizzato l'intero motore delle risorse per utilizzare **interi a 64-bit senza segno (`uint64_t`)**.
    *   **Capacità Maggiorata**: `MAX_ENERGY_CAPACITY` elevato a **999.999.999.999** unità.
    *   **Protezione Underflow**: Tutta la logica di consumo (Combattimento, Navigazione, Drenaggio) utilizza ora un pattern di "Sottrazione Sicura": `if (energy >= cost) energy -= cost; else energy = 0;`. Questo previene il "wrap-around" degli unsigned che garantirebbe energia infinita dopo l'esaurimento.
    *   **Overhaul Effetti Visivi (Smantellamento)**: Potenziato il sistema particellare del comando `dis` con un aumento di 6 volte della dimensione dei frammenti, fisica di espansione ottimizzata e mappatura accurata dei Colori di Fazione per un feedback tattico ad alta fedeltà.
    *   **Sincronizzazione Stato al Login**: Ottimizzato l'handshake di rete per forzare una sincronizzazione totale immediata al rientro in gioco. Questo garantisce che i flag tattici persistenti (Bussola AR, Griglia, modalità HUD) siano ripristinati correttamente nel Visualizzatore 3D fin dal primo frame.
    *   **Layout Binario e Versionamento**: Aggiornato `GALAXY_VERSION` a **20260218**. Questo cambiamento richiede la generazione di un nuovo file `galaxy.dat` per mantenere l'integrità binaria con le nuove strutture dati a 64-bit.
    *   **Sincronizzazione Binaria**: Riallineati i pacchetti di rete e la memoria condivisa (SHM) per garantire la compatibilità zero-copy con il nuovo layout a 64-bit.
    *   **Impatto**: Supporto per riserve energetiche astronomiche e stabilità logica assoluta durante la deplezione delle risorse.

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
*   **Diagnostica di Generazione**: In fase di creazione di una nuova galassia, il sistema produce un **Rapporto Astrometrico** dettagliato, includendo il censimento dei tipi di pianeti (basato sulle risorse), il breakdown dei relitti per classe e fazione, e la mappatura delle minacce di Classe-Omega.

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
*   **HUD Tattico Dinamico**: Implementa una proiezione 2D-su-3D (via `gluProject`) per ancorare etichette, barre della salute e identificativi direttamente sopra i vascelli.
    *   **Indicatori Ship Status**: L'HUD mostra ora gli stati operativi in tempo reale:
        *   `DOCKED`: Stato verde, nave ancorata alla base.
        *   `RED ALERT`: Rosso pulsante, postazioni di combattimento attive, potenza ottimizzata.
        *   `HYPERDRIVE / IMPULSE`: Modalità di propulsione attive.
        *   `ORBITING / SLINGSHOT`: Meccaniche orbitali specializzate.
        *   `DRIFTING (EMERGENCY)`: Inerzia cinetica dovuta a perdita di potenza o guasto ai motori.
    *   **Guerra Elettronica (EWAR)**: Un avviso dedicato appare se i sensori sono disturbati da tempeste ioniche o emettitori ostili.
    *   **Equipaggio (CREW)**: Monitoraggio in tempo reale del personale, vitale per la sopravvivenza della missione.
*   **Engine degli Effetti (VFX)**:
    *   **Trail Engine**: Ogni nave lascia una scia ionica persistente che aiuta a visualizzarne il vettore di movimento.
    *   **Tipologie di Esplosioni ed Effetti Tattici**:
        *   💥 **Esplosione Volumetrica Standard**: Innescata dalla distruzione di un vascello. Una sfera arancione/gialla in rapida espansione con rilascio di oltre 100 particelle calde persistenti.
        *   🌠 **Impatto Siluro al Plasma**: Detonazione ad alta energia che genera un flash cromatico derivato dal nucleo di singolarità del siluro.
        *   ✨ **Particelle di Smantellamento**: Utilizzate durante il comando `dis` o l'abbordaggio. Il relitto si dissolve in una nuvola di frammenti che ereditano i colori della fazione originale.
        *   🔥 **Evento Supernova**: Un segnale cataclismatico globale rappresentato da un enorme cubo rosso pulsante nel settore interessato, con viraggio cromatico dell'intero spazio circostante.
        *   ⚡ **Materializzazione di Salto**: Un flash di radiazione di Hawking bianco brillante che precede la comparsa fisica di un vascello in uscita da un Wormhole.
        *   🔧 **Feedback di Impatto (Hit Sparks)**: Scintille azzurre per gli impatti sugli scudi e frammenti metallici (acciaio, rame, bianco incandescente) per i danni diretti allo scafo.
        *   🌟 **Fascio di Recupero**: Un raggio trasportatore dorato verticale per le operazioni di raccolta risorse e cargo.
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

### 📈 Analisi di Scalabilità

Space GL è progettato per scalare con l'hardware moderno. La seguente analisi valuta l'impatto dell'espansione della matrice galattica ($N \times N \times N$) su un sistema di riferimento con **16GB di RAM** e una **CPU a 16 core**.

#### 1. Vincoli delle Risorse
*   **Memoria (Indice Spaziale)**: Ogni cella dell'indice dei quadranti occupa circa **2,6 KB**.
*   **Larghezza di Banda Memoria**: La funzione `rebuild_spatial_index()` viene eseguita a **30 Hz** sotto un `game_mutex` globale. Matrici grandi richiedono una larghezza di banda DDR4/DDR5 significativa.
*   **Rete**: La matrice della griglia BPNBS utilizza 8 byte per quadrante. La sincronizzazione completa (`UPD_FULL`) scala linearmente con $N^3$.

#### 2. Scenari di Configurazione

| Parametro | 10x10x10 (Standard) | 40x40x40 (Ottimale) | 50x50x50 (Vasta) | 100x100x100 (Estrema) |
| :--- | :--- | :--- | :--- | :--- |
| **Quadranti Totali** | 1.000 | 64.000 | 125.000 | 1.000.000 |
| **RAM Indice** | ~3 MB | ~166 MB | ~325 MB | ~2,6 GB |
| **RAM per Utente** | ~200 KB | ~1,2 MB | ~2,5 MB | ~16 MB |
| **Traffico UPD_FULL** | 8 KB | 512 KB | 1 MB | 8 MB |
| **Carico Bus (stima)** | Trascurabile | 5 GB/s | 9,7 GB/s | 78 GB/s (Saturazione) |
| **Stato** | **Leggero** | **Consigliato** | **Limite Pratico** | **Rischio Stutter** |

### 3. Impatto Multiutente (32+ Giocatori)
*   **Contesa del Mutex**: La ricostruzione di un indice 100x100x100 richiede >33ms, causando potenzialmente cali di Ticks Per Second (TPS) e lag dell'input.
*   **Densità di Interazione**: Espandere la matrice senza aumentare il numero di oggetti diluisce la galassia di un fattore 1.000, rendendo rari gli incontri casuali.
*   **Raccomandazione**: La configurazione **40x40x40** è l'obiettivo consigliato per i moderni server di fascia alta, fornendo un universo massiccio di 64.000 quadranti con latenza quasi zero e un sovraccarico di rete minimo.

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
*   **Interferenza Elettronica (Jamming)**: In presenza di tempeste ioniche o nemici specializzati, i sensori possono essere **disturbati**. La mappa galattica diventa illeggibile e i calcoli di navigazione vengono corrotti.
*   **Fionda Gravitazionale (Slingshot)**: Volare ad alta velocità vicino a stelle o buchi neri può innescare un'accelerazione gravitazionale gratuita, con rischio di danni strutturali.
*   **Deriva di Emergenza (Drift)**: In assenza di energia o propulsione, la nave entra in uno stato di deriva inerziale fino al completo arresto.
*   **Campi di Asteroidi**: Detriti rocciosi che rappresentano un rischio fisico. Il danno da collisione aumenta con la velocità della nave.
*   **Mine Spaziali**: Ordigni esplosivi occulti piazzati da fazioni ostili. Rilevabili solo tramite scansione ravvicinata.
*   **Relitti alla Deriva (Derelicts)**: Gusci di navi distrutte. Possono essere smantellati (`dis`) per recuperare componenti e risorse.
*   **Boe di Comunicazione**: Nodi della rete del Comando dell'Alleanza. Trovarsi vicino potenzia i sensori a lungo raggio (`lrs`), fornendo dati dettagliati sulla composizione dei quadranti adiacenti.
*   **Piattaforme Difensive (Turrets)**: Sentinelle automatiche pesantemente armate che proteggono aree di interesse strategico.

### 👾 Anomalie e Creature
*   **Mostri Spaziali**: Include l'**Entità Cristallina** e l'**Ameba Spaziale**, creature uniche che cacciano attivamente i vascelli per nutrirsi della loro energia.
*   **Tempeste Ioniche**: Fenomeni meteorologici che si spostano nella galassia, capaci di accecare i sensori e deviare la rotta delle navi.

## 💎 Guida alle Risorse e ai Materiali

La galassia è ricca di materie prime ed elementi esotici essenziali per la manutenzione della nave e le operazioni avanzate.

| ID | Risorsa | Fonte Primaria | Metodo di Estrazione | Utilizzo Principale |
| :--- | :--- | :--- | :--- | :--- |
| **1** | **Aetherium** | Buchi Neri / Relitti | `har` / `bor` | Salti Hyperdrive (`jum`), Conversione energia |
| **2** | **Neo-Titanium** | Pianeti / Relitti | `min` / `dis` | Riparazione sistemi (`rep`), Conversione energia |
| **3** | **Void-Essence** | Pianeti / Asteroidi | `min` | Produzione testate per siluri (`con 3`) |
| **4** | **Grafene** | Pianeti / Asteroidi | `min` | **Riparazioni Scafo** (`fix`) |
| **5** | **Synaptics** | Relitti / Detriti | `bor` / `dis` | Riparazioni complesse (`rep`) |
| **6** | **Gas Nebulare** | Comete / Nebulose | `cha` (Coda) / `min` | Conversione energetica efficiente (`con 6`) |
| **7** | **Composite** | Pianeti Classe-H | `min` | **Rinforzo dello Scafo** (`hull`) |
| **8** | **Dark-Matter** | Asteroidi Rari / Stelle | `min` / `sco` | Fonte di energia ultra-densa (`con 8`) |

### Dettaglio Reperimento Risorse
*   **Estrazione Planetaria (`min`)**: Scansionando i pianeti con `scan` si rivela il tipo di risorsa. Usa `min` a breve distanza (< 3.1) per estrarre i minerali. Il **Composite** si trova tipicamente sui pianeti di Classe-H.
*   **Smantellamento Relitti (`dis`)**: Dopo un combattimento, usa `dis` sui detriti dei vascelli per recuperare **Neo-Titanium** e **Chip Synaptics**.
*   **Operazioni d'Abbordaggio (`bor`)**: Inviare squadre su navi alla deriva o nemici disabilitati può fruttare componenti ad alta tecnologia come i **Synaptics** o rari cristalli di **Aetherium**.
*   **Intercettazione Comete**: Inseguendo una cometa (`cha`) e volando attraverso la sua coda (< 0.6 unità) raccoglierai automaticamente **Gas Nebulare**.

---

## 🕹️ Manuale Operativo dei Comandi

Di seguito la lista completa dei comandi disponibili, raggruppati per funzione.

### 🚀 Navigazione
*   `nav <H> <M> <Dist> [Fattore]`: **Navigazione Hyperdrive ad Alta Precisione**. Imposta rotta, distanza precisa e velocità opzionale.
    *   **Requisiti**: Minimo 50% di integrità per **Hyperdrive (ID 0)** e **Sensori (ID 2)**.
    *   **Costo**: 5000 unità di Energia e 1 **Cristallo di Aetherium** per l'attivazione.
    *   **Dinamica**: Consumo energetico continuo proporzionale al fattore. Uscita automatica dall'iperspazio se l'integrità scende sotto il 50% o l'energia si esaurisce.
    *   `H`: Heading (0-359).
    *   `M`: Mark (-90 a +90).
    *   `Dist`: Distanza in Quadranti (supporta decimali, es. `1.73`).
    *   `Fattore`: (Opzionale) Fattore Hyperdrive da 1.0 a 9.9 (Default: 6.0).
*   `imp <H> <M> <S> [Dist]`: **Motore a Impulso**. Motori sub-luce. `S` rappresenta la velocità da 0.0 a 10.0.
    *   **Requisiti**: Minimo 10% di integrità del sistema Impulse (ID 1).
    *   **Costo**: 100 unità di Energia per l'inizializzazione (50 per aggiornamenti di sola velocità).
    *   **Arresto di Precisione**: Se viene fornito il parametro opzionale `[Dist]`, il vascello spegnerà automaticamente i motori una volta raggiunte le coordinate di destinazione.
    *   `S`: Velocità (0.0 - 40.0). `imp 0` per All Stop (Arresto).
*   `pos <H> <M>`: **Posizionamento (Allineamento)**. Orienta la nave su un determinato Heading e Mark senza attivare i motori. 
    *   **Requisiti**: Minimo 10% di integrità del sistema Impulse (ID 1).
    *   **Costo**: 20 unità di Energia.
    *   Utile per puntare obiettivi prima di un attacco o di un salto.
*   `cal <QX> <QY> <QZ> [SX SY SZ]`: **Computer di Navigazione (Alta Precisione)**. Genera un rapporto completo con Heading, Mark e una **Tabella di Confronto delle Velocità**.
    *   **Requisiti**: Minimo 10% di integrità del sistema Computer (ID 6).
    *   **Sincronizzazione Reale**: Le stime temporali sono perfettamente allineate con la velocità costante della nave (Fattore 9.9 = diagonale galattica in 10s).
    *   **Affidabilità Dati**: Se l'integrità del computer è inferiore al 50%, i calcoli potrebbero fallire o restituire risultati corrotti.
*   `ical <X> <Y> <Z>`: **Calcolatore d'Impulso (ETA)**. Calcola H, M ed ETA per raggiungere coordinate precise (0.0-40.0) all'interno del quadrante attuale.
    *   **Requisiti**: Minimo 10% di integrità del sistema Computer (ID 6).
    *   **Consapevolezza Tattica**: Il calcolo dell'ETA tiene conto dell'**Allocazione di Potenza** attuale e dell'**Integrità dei Motori**.
    *   **Suggerimento di Precisione**: Il computer genera un comando `imp` completo che include la distanza necessaria per un **arresto automatico** a destinazione.
    *   Calcola il vettore e il tempo di viaggio basandosi sull'attuale allocazione di potenza ai motori.
    
    *   `jum <QX> <QY> <QZ>`: **Salto Wormhole (Ponte di Einstein-Rosen)**. Genera un wormhole per un salto istantaneo verso il quadrante di destinazione.
    *   **Requisiti**: 5000 unità di Energia, 1 Cristallo di Aetherium e minimo 50% di salute per i sistemi **Hyperdrive (ID 0)** e **Sensori (ID 2)**.
    *   **Rischi**: Possibilità di collasso del wormhole e perdita di risorse se l'integrità è inferiore al 75%.
    *   **Dinamiche**: Causa l'1-3% di danni da stress strutturale allo scafo all'attivazione.
    *   **Procedura**: Richiede una sequenza di stabilizzazione della singolarità di circa 15 secondi.
*   `apr [ID] [DIST]`: **Autopilota di Avvicinamento**. Avvicinamento automatico al bersaglio ID fino alla distanza DIST.
    *   **Requisiti**: Minimo 10% di integrità per i sistemi **Impulse (ID 1)** e **Computer (ID 6)**.
    *   **Costo**: 100 unità di Energia per l'ingaggio dell'autopilota.
    *   **Validazione**: Il bersaglio deve essere rilevabile dai sensori e non occultato (a meno che non appartenga alla propria fazione).
    *   **Limitazione Quadrante**: L'attivazione dell'autopilota è limitata agli oggetti presenti nel quadrante attuale della nave per garantire la sicurezza della navigazione.
    *   Se non viene fornito un ID, utilizza il **bersaglio attualmente agganciato**.
    *   Se viene fornito un solo numero, viene interpretato come **distanza** per il bersaglio agganciato (se < 100).
    *   Fornisce una conferma radio specifica menzionando il nome del bersaglio.
*   `cha`: **Autopilota di Inseguimento**. Insegue attivamente il bersaglio agganciato, mantenendo la traiettoria di intercettazione.
    *   **Requisiti**: Minimo 10% di integrità per i sistemi **Impulse (ID 1)** e **Computer (ID 6)**. Richiede un **Aggancio Bersaglio** (`lock`) attivo.
    *   **Costo**: 150 unità di Energia per l'ingaggio dell'inseguimento.
    *   **Dinamiche**: Ricalcola continuamente il vettore di intercettazione per seguire bersagli mobili attraverso la galassia.
*   `rad <MSG>`: **Radio Dello spazio profondo**. Invia un messaggio globale. Usa `@Fazione` per chat di squadra o `#ID` per messaggi privati.
*   `und`: **Sgancio (Undock)**. Rilascia manualmente i morsetti di attracco dalla Base Stellare.
    *   **Requisiti**: Minimo 10% di integrità del sistema Ausiliario (ID 9).
    *   **Costo**: 50 unità di Energia.
    *   **Nota**: L'attivazione dei motori (`nav`/`imp`) attiva comunque lo sgancio automatico.
*   `doc`: **Attracco (Docking)**. Attracca a una Base Stellare (richiede distanza ravvicinata).
    *   **Requisiti**: La Base Stellare deve appartenere alla propria **Fazione**. Minimo 10% di integrità dei sistemi Impulse (ID 1) e Ausiliari (ID 9).
    *   **Costo**: 100 unità di Energia per l'ingaggio dei morsetti.
    *   **Procedura**: Richiede una **sequenza di stabilizzazione di 10 secondi**. La nave deve rimanere ferma e a portata.
    *   **Effetto**: Riparazione completa di Scafo, Scudi e tutti i 10 Sottosistemi. Rifornimento completo di Energia, Siluri ed Equipaggio. Sbarco di eventuali prigionieri.
*   `map [FILTRO]`: **Cartografia Stellare**. Attiva la visualizzazione 3D globale 10x10x10 dell'intera galassia.
    *   **Requisiti**: Minimo 15% di integrità del sistema Computer (ID 6). Disattivata automaticamente se l'integrità scende sotto il 5%.
    *   **Costo**: 50 unità di Energia per ogni attivazione o cambio filtro.
    *   **Orientamento**: 
        *   **Vettore Tattico 3D**: Una linea gialla sottile con una punta a cono rossa indica la direzione (Heading e Mark) della nave direttamente nella mappa.
        *   **Bussola Galattica 3D**: Un sistema di assi sincronizzato (Blu: 0°, Rosso: 90°, Verde: Mark) è visualizzato in alto al centro per il riferimento spaziale assoluto.
    *   **Filtri Opzionali**: Puoi visualizzare solo categorie specifiche usando: `map st` (Stelle), `map pl` (Pianeti), `map bs` (Basi), `map en` (Nemici), `map bh` (Buchi Neri), `map ne` (Nebulose), `map pu` (Pulsar), `map is` (Tempeste), `map co` (Comete), `map as` (Asteroidi), `map de` (Relitti), `map mi` (Mine), `map bu` (Boe), `map pf` (Piattaforme), `map ri` (Rift), `map mo` (Mostri).
    *   **HUD Verticale**: In modalità mappa, una legenda a sinistra mostra i colori e i codici filtro per ogni oggetto.
    *   **Anomalie Dinamiche**:
        *   **Tempeste Ioniche**: Quadranti racchiusi in un guscio wireframe bianco trasparente.
        *   **Supernova**: Un **grande cubo rosso pulsante** indica un'imminente esplosione stellare nel quadrante (pericolo estremo).
    *   **Localizzazione**: La posizione attuale della nave è indicata da un **indicatore bianco pulsante**, facilitando la navigazione a lungo raggio.

### 🔬 Sensori e Scanner
*   `scan <ID>`: **Analisi Scansione Profonda**. Esegue una scansione profonda del bersaglio o dell'anomalia.
    *   **Requisiti**: Minimo 20% di integrità del sistema Sensori (ID 2).
    *   **Costo**: 20 unità di Energia per ogni scansione profonda.
    *   **Scrambling dei Dati**: Se l'integrità dei sensori è inferiore al 50%, alcuni dati telemetrici (come percentuali esatte o livelli di energia) appariranno offuscati o mancanti.
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
    *   **Requisiti**: Minimo 5% di integrità del sistema Sensori (ID 2).
    *   **Costo**: 10 unità di Energia per scansione.
    *   **Rumore Telemetrico**: Se l'integrità dei sensori è inferiore al 50%, viene emesso un avviso e la precisione dei dati inizia a degradare.
    *   **Analisi Asteroidi**: I sensori forniscono un'analisi immediata della composizione minerale per tutti gli asteroidi rilevati nel quadrante.
    *   **Scansione del Vicinato**: Se la nave è vicina ai confini del settore (< 2.5 unità), i sensori rilevano automaticamente gli oggetti nei quadranti adiacenti, elencandoli in una sezione dedicata per prevenire imboscate.
*   `lrs`: **Sensori a Lungo Raggio**. Scansione 3x3x3 dei quadranti circostanti visualizzata tramite **Console Tattica GDIS**.
    *   **Requisiti**: Minimo 15% di integrità del sistema Sensori (ID 2).
    *   **Costo**: 25 unità di Energia per scansione.
    *   **Instabilità Telemetrica**: Se l'integrità dei sensori è inferiore al 50%, la precisione dei dati degrada e viene emesso un avviso.
    *   **Layout**: Ogni quadrante è visualizzato su una singola riga per un'immediata leggibilità (Coordinate, Navigazione, Oggetti e Anomalie).
    *   **Dati Standard**: Mostra solo la presenza di oggetti usando le loro iniziali (es. `[H . N . S]`).
    *   **Dati Potenziati**: Se la nave è vicino a una **Boa di Comunicazione** (< 1.2 unità), i sensori passano alla visualizzazione numerica rivelando il conteggio esatto (es. `[1 . 2 . 8]`). Il potenziamento si resetta quando ci si allontana dalla boa.
    *   **Soluzione di Navigazione**: Ogni quadrante include i parametri `H / M / W` calcolati per raggiungerlo immediatamente.
    *   **Legenda Primaria**: `[ H P N B S ]` (Buchi Neri, Pianeti, NPC/Vascelle, Basi, Stelle). `N` conta tutte le navi (NPC e altri giocatori); il tuo vascello è automaticamente escluso dal conteggio del quadrante locale.
    *   **Simbologia Anomalie**: `~`:Nebulosa, `*`:Pulsar, `!`:Tempesta Ionica, `+`:Cometa, `#`:Asteroide, `M`:Mostro, `>`:Rift.
    *   **Localizzazione**: Il tuo quadrante attuale è evidenziato con uno sfondo blu.
*   `aux`: **Panoramica Sistemi Ausiliari**. Visualizza lo stato di tutte le sonde sensoriali attive.
*   `aux probe <QX> <QY> <QZ>`: **Sonda Sensoriale Dello spazio profondo**. Lancia una sonda automatizzata in un quadrante specifico.
    *   **Requisiti**: Minimo 10% di integrità del sistema Ausiliario (ID 9). Il lancio richiede il 25% di salute per i **Sensori (ID 2)** e il **Computer (ID 6)**.
    *   **Costo**: 1000 unità di Energia per il lancio.
    *   **Tempo di Viaggio**: Le sonde viaggiano fisicamente attraverso la galassia. Verrai notificato via radio quando la sonda raggiungerà la destinazione. Solo allora potrai richiedere un rapporto telemetrico.
    *   **Feedback Visivo**: Le sonde attive nel tuo settore attuale vengono renderizzate con modelli 3D unici basati sullo slot di appartenenza:
        *   **Sonda 1**: Cubo ciano pulsante con nucleo luminoso.
        *   **Sonda 2**: Sfera verde con tre anelli azzurri rotanti.
        *   **Sonda 3**: Sfera arancione con tre quadrati wireframe in orbita.
    *   Tutti i modelli sono scalati per un'alta visibilità durante le operazioni nello spazio profondo.
*   `aux report <1-3>`: **Telemetria Sonda**. Richiede un rapporto scientifico dettagliato da una sonda attiva, inclusa la classificazione di nebulose e pulsar. Costo: 50 Energia.
    *   **Condizione**: La sonda deve aver raggiunto la destinazione (Stato: ACTIVE). Richiedere un rapporto mentre la sonda è ancora in viaggio fallirà.
*   `aux recover <1-3>`: **Recupero Sonda**. Recupera una sonda se la nave è nello stesso quadrante e a portata (< 2.0 unità), liberando lo slot e ripristinando 500 unità di energia.
*   `aux jettison`: **Espulsione d'Emergenza Hyperdrive**. Espelle il nucleo Hyperdrive per prevenire una breccia imminente nel reattore.
    *   **Effetto**: Distrugge istantaneamente l'**Hyperdrive (ID 0)** e causa massicci **danni allo Scafo e ai Sistemi**. La nave rimarrà alla deriva e incapace di saltare fino alle riparazioni in una Base Stellare. Richiede 1000 Energia.
*   `sta`: **Rapporto di Stato**. Rapporto completo sullo stato della nave, la missione e il monitoraggio dell'**Equipaggio**.
    *   **Requisiti**: Minimo 5% di integrità del sistema Computer (ID 6).
    *   **Costo**: 10 unità di Energia per la diagnostica completa dei sistemi.
    *   **Affidabilità Dati**: Se l'integrità del computer è inferiore al 30%, i dati diagnostici (inclusi telemetria e conteggio equipaggio) potrebbero apparire corrotti o casuali.
    *   **Copertura**: Fornisce il tracciamento in tempo reale dei livelli del Reattore, riserve di Carico, griglie degli Scudi e l'integrità di tutti i 10 sottosistemi della nave.
*   `dam`: **Rapporto Danni**. Dettaglio dei danni ai sistemi.
*   `who`: Elenco dei capitani attivi nella galassia.

### ⚔️ Combattimento Tattico
*   `pha <E>`: **Fuoco Ion Beam**. Spara Ion Beam al bersaglio agganciato (`lock`) usando l'energia E. 
    *   **Requisiti**: Minimo 10% di integrità del sistema Ion Beam (ID 4).
    *   **Precisione**: Possibilità di mancare il bersaglio se i **Sensori (ID 2)** sono danneggiati (sotto il 50% di salute).
    *   **Rischio Hardware**: Sparare con più di 10.000 unità di energia comporta un rischio del 5% di surriscaldamento/danni minori al sistema.
*   `pha <ID> <E>`: Spara Ion Beam a uno specifico bersaglio ID. Il danno diminuisce con la distanza.
*   `cha`: **Inseguimento**. Insegue e intercetta automaticamente il bersaglio agganciato.
*   `rad <MSG>`: **Radio**. Invia un messaggio Dello spazio profondo ad altri capitani (@Fazione per chat di squadra).
*   `axs`: **Bussola Tattica AR**. Attiva/disattiva la bussola olografica in realtà aumentata.
    *   **Requisiti**: Minimo 10% di integrità del sistema Computer (ID 6). Disattivata automaticamente se l'integrità scende sotto il 5%.
    *   **Costo**: 10 unità di Energia per ogni commutazione.
*   `grd`: **Griglia Tattica**. Attiva/disattiva la griglia tattica 3D sovrapposta.
    *   **Requisiti**: Minimo 10% di integrità del sistema Computer (ID 6). Disattivata automaticamente se l'integrità scende sotto il 5%.
    *   **Costo**: 10 unità di Energia per ogni commutazione.
*   `bridge [view]`: **Vista Ponte**. Attiva o cambia la vista cinematografica in prima persona.
    *   **Requisiti**: Minimo 10% di integrità del sistema Computer (ID 6). Disattivata automaticamente se l'integrità scende sotto il 5%.
    *   **Costo**: 10 unità di Energia per ogni cambio di visuale.
    *   **Parametri**: Supporta `top`, `bottom`, `left`, `right`, `up`, `down`, `rear`, `off`. La visuale mantiene intelligentemente la prospettiva tra il ponte superiore e quello inferiore.
    *   `top/on`: Vista ponte standard (Frontale).
    *   `bottom`: Prospettiva da sotto lo scafo (Frontale).
    *   `up/down/left/right/rear`: Cambia la direzione dello sguardo rispetto alla prospettiva attuale.
    *   `off`: Disattiva la vista ponte.
*   `enc <algo>`: **Commutazione Crittografia**. Abilita o disabilita la crittografia in tempo reale. 
    *   **Requisiti**: Minimo 10% di integrità del sistema Computer (ID 6). **PQC (ML-KEM)** richiede minimo 50% di integrità.
    *   **Costo**: 50 unità di Energia per gli algoritmi standard, 250 unità per PQC. Disabilitare la cifratura (`enc off`) è gratuito.
    *   Supporta **AES-256-GCM**, **ChaCha20**, **ARIA**, **Camellia**, **Blowfish**, **RC4**, **CAST5**, **IDEA**, **3DES** e **PQC (ML-KEM)**. Essenziale per proteggere le comunicazioni e leggere i messaggi sicuri degli altri capitani.
*   `tor`: **Lancio Siluro al plasma**. Lancia un siluro autoguidato al bersaglio agganciato.
    *   **Sistema a 4 Tubi**: Il vascello dispone di 4 tubi di lancio indipendenti con rotazione automatica. È possibile lanciare fino a 4 siluri in rapida successione.
    *   **Visibilità Universale**: I siluri in volo sono visibili sui visori 3D di tutti i giocatori nel settore.
    *   **Confini Settore**: I siluri esplodono istantaneamente se raggiungono i confini del quadrante attuale (0.0 - 40.0).
    *   **Requisiti**: Minimo 50% di integrità del sistema Siluri (ID 5).
    *   **Costo**: 250 unità di Energia per lancio.
    *   **Ricarica**: Ogni tubo richiede circa 3 secondi (90 tick) per la ricarica dopo il lancio.
    *   **Rischi**: Possibilità di malfunzionamento (siluro perso, nessun lancio) se l'integrità è inferiore al 75%.
    *   **Guida**: L'accuratezza del sistema di autoguida dipende dalla salute dei **Sensori (ID 2)**. Sensori danneggiati riducono la capacità del siluro di correggere la propria rotta.
*   `tor <H> <M>`: Lancia un siluro in modalità balistica manuale (Heading/Mark).
*   `lock <ID>`: **Aggancio Bersaglio**. Aggancia i sistemi di puntamento sul bersaglio ID.
    *   **Rilascio manuale**: Usa `lock off` per liberare i sistemi di puntamento.
    *   **Requisiti**: Minimo 10% di integrità del sistema Sensori (ID 2).
    *   **Costo**: 5 unità di Energia per l'acquisizione dell'aggancio.
    *   **Validazione e Restrizioni**: Il bersaglio deve essere presente nel **quadrante attuale** e non occultato (a meno che non appartenga alla propria fazione).
    *   **Rilascio Automatico**: Il lock si disinserisce istantaneamente se il bersaglio viene distrutto, esce dal settore o se la nave del giocatore cambia quadrante.
    *   Essenziale per la guida automatizzata di Ion Beam e siluri.


### 🆔 Schema Identificativi Galattici (Universal ID)
Per interagire con gli oggetti galattici usando i comandi `lock`, `scan`, `pha`, `tor`, `bor` e `dis`, il sistema utilizza un sistema di ID unico. Usa il comando `srs` per identificare gli ID degli oggetti nel tuo settore.

| Categoria | Intervallo ID | Esempio | Utilizzo Primario |
| :--- | :--- | :--- | :--- |
| **Giocatore** | 1 - 999 | `lock 1` | Tuo vascello o altri giocatori |
| **NPC (Nemico)** | 1.000 - 3.999 | `lock 1050` | Inseguimento (`cha`) e combattimento |
| **Basi Stellari** | 4.000 - 4.999 | `lock 4005` | Attracco (`doc`) e rifornimento |
| **Pianeti** | 5.000 - 6.999 | `lock 5012` | Estrazione planetaria (`min`) |
| **Stelle** | 7.000 - 10.999 | `lock 7500` | Ricarica solare (`sco`) |
| **Buchi Neri** | 11.000 - 11.999 | `lock 11001` | Raccolta Plasma Reserves (`har`) |
| **Nebulose** | 12.000 - 12.999 | `lock 12000` | Analisi scientifica e copertura |
| **Pulsar** | 13.000 - 13.999 | `lock 13000` | Monitoraggio radiazioni |
| **Comete** | 14.000 - 14.999 | `lock 14001` | Inseguimento e raccolta gas rari |
| **Relitti** | 15.000 - 17.999 | `lock 15005` | Abbordaggio (`bor`) e recupero tech |
| **Asteroidi** | 18.000 - 20.499 | `lock 18000` | Navigazione di precisione |
| **Mine** | 20.500 - 21.999 | `lock 20500` | Allerta tattica ed evitamento |
| **Boe Comm.** | 22.000 - 22.999 | `lock 22000` | Link dati e potenziamento `lrs` |
| **Piattaforme** | 23.000 - 23.999 | `lock 23000` | Distruzione sentinelle ostili |
| **Rift Spaziali** | 24.000 - 24.999 | `lock 24000` | Utilizzo per salti casuali |
| **Mostri** | 25.000 - 25.999 | `lock 25000` | Scenari di combattimento estremo |
| **Sonde** | 26.000 - 26.999 | `apr 26000` | Recupero e telemetria automatizzata |

**Nota**: L'aggancio e l'autopilota (`apr`) funzionano **esclusivamente** se l'oggetto è nel tuo quadrante attuale. Se l'ID esiste ma è lontano, il computer indicherà le coordinate `Q[x,y,z]` del bersaglio. Questo vincolo garantisce che l'autopilota operi solo su bersagli effettivamente rilevabili dai sensori a corto raggio.

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

**Nota sulla Portata Inter-Settore**: Il comando `dis` (smantellamento) utilizza un sistema di risoluzione globale. Questo significa che puoi puntare e smantellare qualsiasi relitto visibile sul tuo HUD o identificato dai sensori, anche se si trova in un quadrante adiacente al tuo. Il comando `apr` (avvicinamento) è invece limitato agli oggetti presenti nel quadrante attuale per garantire la sicurezza della navigazione a corto raggio.

### 📏 Tabella delle Distanze di Interazione
Distanze espresse in unità di settore (0.0 - 40.0). Se la tua distanza è superiore al limite, il computer risponderà con "No [object] in range".

| Oggetto / Entità | Comando / Azione | Distanza Minima | Effetto / Interazione |
| :--- | :--- | :--- | :--- |
| **Stella** | `sco` | **< 3.1** | Ricarica solare (Solar scooping) |
| **Pianeta** | `min` | **< 3.1** | Estrazione planetaria |
| **Base Stellare** | `doc` | **< 3.1** | Riparazione completa (Scafo, Scudi, Sistemi), ricarica energia, siluri, equipaggio e sbarco prigionieri |
| **Buco Nero** | `har` | **< 3.1** | Raccolta Plasma Reserves |
| **Relitto** | `dis` | **< 1.5** | Smantellamento per risorse |
| **Nave Nemica** | `bor` | **< 1.0** | Operazione squadra d'abbordaggio |
| **Nave Nemica** | `pha` (Fuoco) | **< 6.0** | Gittata massima Ion Beam NPC |
| **Siluro al plasma** | (Impatto) | **< 0.5** | Distanza di collisione per detonazione |
| **Boa Comm.** | (Passivo) | **< 1.2** | Potenziamento segnale o messaggi auto |
| **Ameba Spaziale** | (Contatto) | **< 1.5** | Inizio drenaggio energetico critico |
| **Entità Cristallina**| (Risonanza) | **< 4.0** | Gittata del raggio di risonanza |
| **Corpo Celeste** | (Collisione) | **< 1.0** | Danni scafo e attivazione soccorso d'emergenza |

### 🚀 Autopilota (`apr`)
Il comando `apr <ID> <DIST>` ti permette di avvicinarti automaticamente a qualsiasi oggetto rilevato dai sensori nel tuo quadrante attuale.

| Categoria Oggetto | Intervallo ID | Comandi di Interazione | Dist. Min. | Note di Navigazione |
| :--- | :--- | :--- | :--- | :--- |
| **Capitani (Giocatori)** | 1 - 999 | `rad`, `pha`, `tor`, `bor` | **< 1.0** (`bor`) | Solo quadrante attuale |
| **Navi NPC (Alieni)** | 1000 - 3999 | `pha`, `tor`, `bor`, `scan` | **< 1.0** (`bor`) | Solo quadrante attuale |
| **Basi Stellari** | 4000 - 4999 | `doc`, `scan` | **< 3.1** | Solo quadrante attuale |
| **Pianeti** | 5000 - 6999 | `min`, `scan` | **< 3.1** | Solo quadrante attuale |
| **Stelle** | 7000 - 10999 | `sco`, `scan` | **< 3.1** | Solo quadrante attuale |
| **Buchi Neri** | 11000 - 11999 | `har`, `scan` | **< 3.1** | Solo quadrante attuale |
| **Nebulose** | 12000 - 12999 | `scan` | - | Solo quadrante attuale |
| **Pulsar** | 13000 - 13999 | `scan` | - | Solo quadrante attuale |
| **Comete** | 14000 - 14999 | `cha`, `scan` | **< 0.6** (Gas) | Solo quadrante attuale |
| **Relitti** | 15000 - 17999 | `bor`, `dis`, `scan` | **< 1.5** | Solo quadrante attuale |
| **Asteroidi** | 18000 - 20499 | `min`, `scan` | **< 3.1** | Solo quadrante attuale |
| **Mine** | 20500 - 21999 | `scan` | - | Solo quadrante attuale |
| **Boe Comm.** | 22000 - 22999 | `scan` | **< 1.2** | Solo quadrante attuale |
| **Piattaforme Difesa** | 23000 - 23999 | `pha`, `tor`, `scan` | - | Solo quadrante attuale |
| **Rift Spaziali** | 24000 - 24999 | `scan` | - | Solo quadrante attuale |
| **Mostri Spaziali** | 25000 - 25999 | `pha`, `tor`, `scan` | **< 1.5** | Solo quadrante attuale |

*   `she <F> <R> <T> <B> <L> <RI>`: **Configurazione Scudi**. Distribuisce l'energia ai 6 scudi.
    *   **Requisiti**: Minimo 10% di integrità del sistema Scudi (ID 8).
    *   **Dinamiche Energetiche**: Caricare gli scudi preleva energia direttamente dal reattore principale. Scaricare gli scudi restituisce l'80% dell'energia al reattore.
    *   **Limiti**: La capacità massima è di 10.000 unità per quadrante.
    *   **Costo**: 50 unità di Energia per ogni impulso di riconfigurazione.
*   `clo`: **Dispositivo di Occultamento**. Attiva/Disattiva l'occultamento. 
    *   **Requisiti**: Minimo 15% di integrità del sistema Ausiliario (ID 9).
    *   **Costo**: 500 unità di Energia per l'inizializzazione.
    *   **Dinamiche**: Consuma 15 unità di energia/tick mentre è attivo. Il disoccultamento automatico avviene se l'integrità Ausiliaria scende sotto il 5%.
    *   **Affidabilità**: Se la salute degli Ausiliari è tra il 15% e il 50%, c'è una probabilità che il campo non riesca a stabilizzarsi durante l'attivazione.
    *   Fornisce invisibilità agli NPC e alle altre fazioni; instabile nelle nebulose. Consuma energia e limita i sensori.
*   `pow <E> <S> <W>`: **Allocazione di Potenza**. Alloca l'energia del reattore (Motori, Scudi, Armi %).
    *   **Requisiti**: Minimo 10% di integrità del sistema Computer (ID 6).
    *   **Costo**: 50 unità di Energia per ogni riconfigurazione.
    *   **Feedback**: Fornisce una conferma immediata della nuova distribuzione percentuale.
    *   **Impatto Strategico**: Determina le prestazioni dei motori sub-luce, il tasso di ricarica degli scudi e l'intensità dei banchi Ion Beam.
*   `aux jettison`: **Espulsione Synaptics Hyperdrive**. Espelle il nucleo (Manovra suicida / Ultima risorsa).
*   `xxx`: **Riposizionamento di Emergenza**. Attiva un **Tactical Warp** immediato verso un settore sicuro, lasciando un relitto dello scafo precedente. Ripristina sistemi ed energia all'80%.
*   `zztop`: **Cancellazione Totale Profilo**. Purga permanentemente il nome del capitano e tutti i dati della carriera dal database della galassia. Questa azione è irreversibile e interrompe la connessione.

### ⚡ Gestione del Reattore e della Potenza

Il comando `pow` è fondamentale per la sopravvivenza e la superiorità tattica. Detto comando determina come l'uscita del reattore principale della nave viene ripartita su tre sottosistemi principali:

*   **Motori (E)**: Influenza la reattività e la velocità massima del **Motore a Impulso**. Un'alta allocazione permette manovre rapide e un attraversamento più veloce del settore.
*   **Scudi (S)**: Governa il **Tasso di Rigenerazione** di tutti i 6 quadranti degli scudi. Se gli scudi sono danneggiati, attingono energia dal reattore per ricostruire la loro integrità.
    *   **Scaling Dinamico**: La velocità di rigenerazione è un prodotto sia della **Potenza (S)** assegnata che dell'**Integrità del Sistema Scudi**. Se il generatore di scudi è danneggiato, la rigenerazione sarà gravemente ostacolata indipendentemente dall'allocazione di potenza.

#### 🛡️ Meccanica degli Scudi e Integrità dello Scafo
La nave è protetta da 6 quadranti indipendenti: **Frontale (F), Posteriore (R), Superiore (T), Inferiore (B), Sinistro (L) e Destro (RI)**.

*   **Danni Localizzati**: Gli attacchi (Ion Beam/Siluri) ora colpiscono quadranti specifici in base all'angolo relativo di impatto.
*   **Integrità dello Scafo**: Rappresenta la salute fisica della nave (0-100%). Se un quadrante di scudo raggiunge lo 0% o l'impatto è eccessivamente potente, il danno residuo colpisce direttamente l'integrità strutturale.
*   **Danni ai Sistemi Interni**: Ogni colpo diretto allo scafo (quando gli scudi sono abbassati o bypassati) causa automaticamente una piccola percentuale di danno (1-5%) a un sistema di bordo scelto a caso (Motori, Sensori, Computer, ecc.). Questo rende l'esposizione dello scafo estremamente pericolosa anche contro armi a bassa potenza.
*   **Placcatura dello Scafo (Composite)**: Una placcatura aggiuntiva (comando `hull`) funge da buffer: assorbe i danni fisici *prima* che colpiscano l'integrità dello scafo.
*   **Rientro di Emergenza (Nessun Game Over)**: Se l'**Integrità dello Scafo raggiunge lo 0%** (o l'Equipaggio viene perso), la nave NON viene distrutta permanentemente.
    *   **Trasferimento Scafo**: Lo scafo danneggiato viene espulso e diventa un **relitto permanente** nel settore attuale (recuperabile da altri giocatori).
    *   **Salvataggio**: Verrai istantaneamente trasferito in un **quadrante sicuro** con un vascello sostitutivo.
    *   **Stato**: La nuova nave inizia con **80% di integrità dei sistemi**, energia al massimo e una scorta base di siluri. La tua carriera continua senza disconnessione.
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
*   **Soglia Critica**: Se la percentuale scende sotto il **75%**, l'equipaggio inizierà a subire perdite periodiche.
*   **Gestione Rigorosa**: Il conteggio dell'equipaggio è protetto e non può mai scendere sotto lo zero.
*   **Fine Missione Istantanea**: Se il numero dell'equipaggio raggiunge lo **zero**, il vascello viene dichiarato perso istantaneamente. I sistemi si disattivano, l'energia viene azzerata e viene generata un'esplosione strutturale.
*   **Limiti Tattici**: Durante le operazioni di abbordaggio (`bor`), non è possibile trasferire o catturare più personale di quello effettivamente presente a bordo dei vascelli coinvolti.
*   **Riparazioni di Emergenza**: Mantenere il Supporto Vitale sopra la soglia è la massima priorità. Usa immediatamente `rep 7` se l'integrità è compromessa.

**Feedback HUD**: L'allocazione attuale è visibile nel pannello diagnostico in basso a destra come `POWER: E:XX% S:XX% W:XX%`. Il monitoraggio è essenziale per assicurarsi che la nave sia ottimizzata per la fase di missione corrente (Esplorazione vs. Combattimento).

### 📦 Operazioni e Risorse
*   `bor [ID]`: **Squadra d'Abbordaggio**. Invia squadre d'abbordaggio (Dist < 1.0).
    *   **Requisiti**: Minimo 20% di integrità del sistema Trasportatori (ID 3).
    *   **Costo**: 5000 unità di Energia per tentativo.
    *   **Probabilità di Successo**: Scalata dall'integrità dei Trasportatori (Base 20% + fino al 40%).
    *   **Fallback Intelligente**: Se l'ID non viene specificato e non c'è un `lock` attivo, il comando agirà automaticamente sull'ultimo bersaglio impostato con `apr` (Autopilota).
    *   **Interazione NPC/Relitto**: Apre un **Menu Tattico** specifico:
        *   **Vascelli Ostili (NPC)**: `1`: Sabotaggio Motori (Immobilizzazione), `2`: Raid Stiva (Risorse), `3`: Cattura Prigionieri. **Nota**: L'abbordaggio NPC richiede che il bersaglio sia **disabilitato** (Motori < 50% o Scafo < 50%).
        *   **Relitti/Derelict**: `1`: Recupero Risorse, `2`: Decrittazione Dati Mappa, `3`: Riparazioni d'Emergenza, `4`: Salvataggio Superstiti (Equipaggio). **Nota**: L'abbordaggio dei relitti non ne causa più la distruzione automatica.
    *   **Interazione Giocatore-Giocatore**: Apre un **Menu Tattico Interattivo** con scelte specifiche:
        *   **Vascelli Alleati**: `1`: Trasferisci Energia, `2`: Ripara Sistema, `3`: Invia Rinforzi Equipaggio.
        *   **Vascelli Ostili**: `1`: Sabotaggio Sistema, `2`: Incursione nella Stiva, `3`: Cattura Ostaggi.
    *   **Selezione**: Rispondi con il numero `1`, `2` o `3` per eseguire l'azione.
    *   **Rischi**: Possibilità di resistenza (30% per i giocatori, più alta per gli NPC) che può causare perdite nella squadra.
*   `dis [ID]`: **Smantellamento**. Smantella i relitti nemici per le risorse (Dist < 1.5).
    *   **Requisiti**: Minimo 15% di integrità del sistema Trasportatori (ID 3).
    *   **Costo**: 500 unità di Energia per ogni operazione.
    *   **Resa**: L'efficienza del recupero risorse dipende dalla salute dei **Trasportatori (ID 3)**. Sistemi danneggiati producono una resa inferiore.
    *   **Bersagli**: Funziona sui vascelli NPC distrutti e sui relitti antichi (derelict).
    *   **Fallback Intelligente**: Come per `bor`, supporta il targeting automatico sull'oggetto dell'autopilota (`apr`).
*   `fix`: **Riparazione Scafo**. Usa **50 Grafene e 20 Neo-Titanium** per ripristinare l'integrità dello scafo (fino a un max dell'80%).
    *   **Requisiti**: Minimo 10% di integrità del sistema Ausiliario (ID 9).
    *   **Costo**: 500 unità di Energia per ogni impulso di riparazione.
    *   **Resa**: L'efficienza del ripristino strutturale (range 10% - 20%) dipende dalla salute dei sistemi **Ausiliari (ID 9)**.
    *   **Limite**: Le riparazioni sul campo non possono superare l'80% di integrità totale. È necessario l'attracco in Base Stellare per una revisione completa.
*   `min`: **Estrazione**. Estrae risorse da un pianeta o asteroide in orbita (Dist < 3.1).
    *   **Requisiti**: Minimo 15% di integrità del sistema Trasportatori (ID 3).
    *   **Costo**: 250 unità di Energia per ogni impulso di estrazione.
    *   **Resa**: L'efficienza dell'estrazione mineraria dipende dalla salute dei **Trasportatori (ID 3)**.
    *   **Priorità Selettiva**:
        1.  Se un bersaglio è agganciato (`lock <ID>`), il sistema gli garantisce la priorità assoluta.
        2.  Senza un aggancio, il sistema estrarrà l'oggetto estraibile **assolutamente più vicino**.
    *   **Feedback**: Fornisce una conferma radio dettagliata sulla quantità e sul tipo di risorsa raccolta.
*   `sco`: **Solar Scooping**. Raccoglie energia da una stella (Dist < 3.1).
    *   **Requisiti**: Minimo 15% di integrità del sistema Ausiliario (ID 9).
    *   **Costo**: 100 unità di Energia per ogni ciclo di raccolta.
    *   **Efficienza**: L'energia raccolta (fino a 5000 unità) dipende dalla salute del sistema **Ausiliario (ID 9)**.
    *   **Pericolo**: La raccolta solare genera calore intenso. Se gli scudi sono insufficienti, l'**Integrità dello Scafo** e il **Supporto Vitale** subiranno danni diretti.
*   `har`: **Raccolta Plasma Reserves**. Raccoglie antimateria da un buco nero (Dist < 3.1).
    *   **Requisiti**: Minimo 25% di integrità del sistema Ausiliario (ID 9).
    *   **Costo**: 500 unità di Energia per ogni ciclo di raccolta.
    *   **Efficienza**: L'energia (fino a 10.000 unità) e i cristalli di Aetherium raccolti dipendono dalla salute del sistema **Ausiliario (ID 9)**.
    *   **Pericolo Estremo**: La raccolta presso una singolarità è estremamente rischiosa. Se gli scudi sono insufficienti, la nave subirà pesanti **danni allo scafo** e guasti critici ai sistemi **Sensori**, **Computer** e **Supporto Vitale**.
*   `con T A`: **Conversione Risorse**. Converte materie prime in energia o siluri (`T`: tipo di risorsa, `A`: quantità).
    *   **Requisiti**: Minimo 15% di integrità del sistema Ausiliario (ID 9).
    *   **Costo**: 100 unità di Energia per ogni ciclo di conversione.
    *   **Efficienza**: La resa della conversione dipende dalla salute dei sistemi **Ausiliari (ID 9)** (range 70% - 100%). Sistemi danneggiati causano una perdita significativa di risorse durante il processo.
    *   **Mappatura Risorse**:
        *   `1`: Aetherium -> Energia (base x10).
        *   `2`: Neo-Titanium -> Energia (base x2).
        *   `3`: Void-Essence -> Siluri (base 1 ogni 20).
        *   `6`: Gas -> Energia (base x5).
        *   `7`: Composite -> Energia (base x4).
        *   `8`: **Dark-Matter** -> Energia (base x25). [Massima Efficienza].
*   `load <T> <A>`: **Caricamento Sistemi**. Trasferisce energia o siluri dalla stiva ai sistemi attivi.
    *   **Requisiti**: Minimo 10% di integrità del sistema Ausiliario (ID 9).
    *   **Costo**: 25 unità di Energia per ogni ciclo di trasferimento.
    *   **Tipi**:
        *   `1`: Energia (Reattore Principale). Capacità max: 999.999.999.999 unità.
        *   `2`: Siluri (Tubi di Lancio). Capacità max: 1000 unità.
    *   **Feedback**: Conferma l'esatta quantità di risorse spostate nei sistemi operativi.

*   `hull`: **Rinforzo Scafo**. Usa **100 unità di Composite** per applicare una placcatura rinforzata allo scafo (+500 unità di integrità).
    *   **Requisiti**: Minimo 10% di integrità del sistema Ausiliario (ID 9).
    *   **Costo**: 1000 unità di Energia per ogni ciclo di integrazione.
    *   **Limite**: La capacità massima della placcatura composita è di 5000 unità.
    *   La placcatura in Composite funge da scudo fisico secondario, assorbendo i danni residui che superano gli scudi energetici prima che colpiscano lo scafo. Lo stato della placcatura è visibile nell'HUD 3D.
*   `inv`: **Rapporto Inventario**. Mostra il contenuto della stiva di carico, inclusi i materiali grezzi (**Grafene**, **Synaptics**, **Composite**) e i **Prigionieri**.
    *   **Requisiti**: Minimo 5% di integrità del sistema Computer (ID 6).
    *   **Costo**: 5 unità di Energia per ogni scansione del manifesto.
    *   **Affidabilità Dati**: Se l'integrità del computer è inferiore al 30%, i dati logistici potrebbero apparire corrotti o offuscati.
    *   **Registro**: Monitora Aetherium, Neo-Titanium, Void-Essence, Grafene, Synaptics, Gas Nebulare, Composite, Dark-Matter, Plasma Reserves, Siluri da Carico e l'Unità Prigione.

### 📦 Gestione del Carico e delle Risorse

Space GL distingue tra **Sistemi Attivi**, **Stoccaggio del Carico** e l'**Unità Prigione**. Questo è riflesso nell'HUD come:
*   **ENERGY: X (CARGO ANTIMATTER: Y)**: Riserve principali vs stoccaggio stiva.
*   **TORPS: X (CARGO TORPEDOES: Y)**: Stato lanciasiluri e inventario (su righe separate).
*   **LOCK: [ID]**: Stato del puntamento bersaglio, sopra il sistema siluri.
*   **HULL INTEGRITY / PLATING**: Integrità scafo e corazzatura.
*   **CREW / PRISON UNIT**: Monitoraggio equipaggio e prigionieri.
*   **SYSTEMS HEALTH**: Griglia diagnostica per i 10 sottosistemi.
*   **CARGO INVENTORY**: Tracciamento per 8 tipi di risorse speciali.
*   **PROBES STATUS**: Stato in tempo reale delle sonde.

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

*   `rep [ID]`: **Riparazione**. Ripara un sistema danneggiato (salute < 100%) ripristinandolo alla piena efficienza. 
    *   **Requisiti**: Minimo 10% di integrità del sistema Computer (ID 6) per coordinare la manutenzione.
    *   **Costo**: 500 unità di Energia per ogni ciclo di riparazione.
    *   **Materiali**: Ogni riparazione consuma **50 Neo-Titanium** e **10 Chip Synaptics**.
    *   **Utilizzo**: Se non viene fornito alcun ID, elenca tutti i 10 sistemi della nave con il loro attuale stato di integrità e indicatori di salute colorati.
    *   **ID dei Sistemi**: `0`: Hyperdrive, `1`: Impulse, `2`: Sensori, `3`: Transp, `4`: Ion Beam, `5`: Torpedini, `6`: Computer, `7`: Supporto Vitale, `8`: Scudi, `9`: Ausiliari.
*   **Gestione dell'Equipaggio**: 
    *   Il numero iniziale del personale dipende dalla classe della nave (es. 1012 per l'Esploratore, 50 per la Scorta).    *   **Integrità Vitale**: Se il **Supporto Vitale** scende sotto il 75%, l'equipaggio inizierà a subire perdite periodiche.
    *   **Integrità degli Scudi**: Se l'integrità del **Sistema Scudi (ID 8)** è bassa, la ricarica automatica dei 6 quadranti è rallentata.
    *   **Condizione di Fallimento**: Se l'equipaggio raggiunge lo **zero**, la missione termina e la nave è considerata persa.

### 3. Guida ai Comandi Operativi e Crittografia Dello spazio profondo

Il ponte di comando di Space GL opera tramite un'interfaccia a riga di comando (CLI) ad alta precisione. Oltre alla navigazione e al combattimento, il simulatore implementa un sofisticato sistema di **Guerra Elettronica** basato sulla crittografia del mondo reale.

**Nota sull'Help**: Il comando `help` è gestito centralmente dal server. Questo garantisce che la directory dei comandi LCARS sia sempre sincronizzata con le ultime capacità tattiche e le specifiche degli ID degli oggetti.

#### 🛰️ Comandi Avanzati di Navigazione e Utilità
*   `red`: **Allarme Rosso**. Commuta lo stato di allerta tattica. Bilancia automaticamente la potenza tra scudi e armi. HUD pulsante rosso.
*   `orb`: **Orbita Planetaria**. Entra in un'orbita stabile attorno al pianeta agganciato (< 1.0 unità). Fornisce stabilità tattica.
*   `nav <H> <M> <W> [F]`: **Navigazione Hyperdrive**. Traccia una rotta Hyperdrive verso coordinate relative. `H`: Heading (0-359), `M`: Mark (-90/+90), `W`: Distanza in quadranti, `F`: Fattore Hyperdrive opzionale (1.0 - 9.9).
*   `imp <H> <M> <S>`: **Motore a Impulso**. Navigazione sub-luce all'interno del settore attuale. `S`: Velocità in percentuale (1-100%). Usa `imp <S>` per regolare solo la velocità.
*   `pos <H> <M>`: **Posizionamento (Allineamento)**. Orienta la nave su Heading/Mark senza movimento.
*   `jum <Q1> <Q2> <Q3>`: **Salto Wormhole**. Genera un tunnel spaziale verso un quadrante distante. Richiede **5000 Energia e 1 Cristallo di Aetherium**.
*   `apr <ID> [DIST]`: **Avvicinamento Automatico**. L'autopilota intercetta l'oggetto specificato alla distanza desiderata (default 2.0). Funziona in tutta la galassia per navi e comete.
*   `cha`: **Inseguimento Bersaglio**. Insegue attivamente il bersaglio attualmente agganciato (`lock`). Per gli NPC ostili, mantiene una distanza di sicurezza di **3.0 unità** se operativi, avvicinandosi a **1.5 unità** solo se disabilitati. Per le comete, mantiene la distanza di raccolta gas (< 0.6).
*   `rep <ID>`: **Riparazione Sistema**. Avvia le riparazioni su un sottosistema (1: Hyperdrive, 2: Impulse, 3: Sensori, 4: Ion Beam, 5: Siluri, ecc.).
*   `fix`: **Riparazione Scafo**. Ripristina +15% di integrità (50 Grafene, 20 Neo-Ti).
*   `inv`: **Rapporto Inventario**. Elenco dettagliato delle risorse nella stiva (Aetherium, Neo-Titanium, Gas Nebulare, ecc.).
*   `dam`: **Rapporto Danni**. Stato dettagliato dell'integrità dello scafo e dei sistemi.
    *   **Requisiti**: Minimo 5% di integrità del sistema Computer (ID 6).
    *   **Costo**: 5 unità di Energia per ogni scansione di integrità.
    *   **Affidabilità Dati**: Se l'integrità del computer è inferiore al 30%, errori di parità dei sensori potrebbero causare l'offuscamento di alcuni dati sullo stato dei sistemi.
    *   **Registro**: Fornisce un monitoraggio preciso per tutti i 10 sottosistemi della nave utilizzando indicatori di stato colorati (Verde/Giallo/Rosso).
*   `cal <Q1> <Q2> <Q3>`: **Calcolatore Hyperdrive**. Calcola il vettore verso il centro di un quadrante distante.
*   `cal <Q1> <Q2> <Q3> <X> <Y> <Z>`: **Calcolatore di Precisione**. Calcola il vettore verso coordinate di settore precise `[X, Y, Z]` in un quadrante distante. Fornisce i tempi di arrivo e suggerisce il comando `nav` esatto da copiare.
*   `ical <X> <Y> <Z>`: **Calcolatore d'Impulso (ETA)**. Fornisce un calcolo di navigazione completo per le coordinate di settore precise [0.0 - 40.0], incluso il tempo di viaggio in tempo reale ai livelli di potenza attuali.
*   `who`: **Registro dei Capitani**. Elenca tutti i comandanti attualmente attivi nella galassia, i loro ID di tracciamento e la posizione attuale. 
    *   **Requisiti**: Minimo 5% di integrità del sistema Computer (ID 6).
    *   **Costo**: 10 unità di Energia per la sincronizzazione subspaziale.
    *   **Affidabilità Dati**: Se l'integrità del computer è inferiore al 40%, la posizione e la fazione dei capitani distanti potrebbero essere oscurate o nascoste.
    *   Fondamentale per identificare alleati o potenziali predatori prima di entrare in un settore.
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
*   `rad <MSG>`: **Radio**. Invia un messaggio radio a tutti (Canale aperto).
    *   **Requisiti**: Minimo 5% di integrità del sistema Ausiliario (ID 9).
    *   **Costo**: 5 unità di Energia per trasmissione.
    *   **Tabella delle Fazioni (@Fac)**:
        | Faction | Nome Completo | Abbreviato |
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
    *   **Requisiti**: Minimo 20% di integrità del sistema Computer (ID 6) e almeno un dispositivo Anti-Materia in inventario.
    *   **Costo**: 500 unità di Energia per ogni trasmissione.
    *   **Effetto**: 60% di probabilità di costringere tutti gli NPC ostili nel quadrante alla fuga.
    *   **Rischio**: Se il bluff fallisce, gli NPC rileveranno l'inganno e diventeranno più aggressivi, passando alla modalità Inseguimento (Chase).
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
Attivando gli assi visivi (`axs`), il simulatore proietta un doppio sistema di riferimento sferico centrato sulla tua nave:
*   **Anello Orizzontale Fisso (Bianco)**: Ancorato al piano galattico (XZ). Include marcatori cardinali (N, E, S, W) e tacche ogni 30° per un orientamento assoluto.
*   **Anello Heading Inclinabile (Ciano)**: Solidale al piano di volo della nave. Si inclina con il beccheggio per fornire un riferimento direzionale costante rispetto alla prua.
*   **Arco del Mark Verticale (Giallo)**: Ancorato allo zenit galattico, permette una lettura precisa del beccheggio (salita/discesa).
*   **Assi Galattici Fissi**: Gli assi X (Rosso), Y (Verde) e Z (Blu) rimangono punti di riferimento assoluti.

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
*   **Coordinate di Settore**: Conversione istantanea dei dati spaziali in coordinate relative `[S1, S2, S3]` (0.0 - 40.0), speculari a quelle usate nei comandi `nav` e `imp`.
*   **🌐 Sistema di Coordinate Galattiche Assolute (GDIS)**:
    *   Per garantire precisione millimetrica, il server utilizza coordinate assolute su un cubo galattico di **100x100x100** unità. 
    *   Tutti i comandi critici (`apr`, `bor`, `pha`, `tor`) utilizzano questo riferimento globale per eliminare errori di raggio d'azione tra settori diversi.
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
Le navi controllate dal computer (Korthian, Xylari, Swarm, ecc.) operano con protocolli di combattimento avanzati:
*   **Armamento Primario**: Attualmente, le navi NPC sono equipaggiate esclusivamente con **Banchi Ion Beam**.
*   **Integrità NPC**: Ogni vascello NPC possiede un sistema di difesa a due livelli:
    *   **Placcatura (Plating)**: Uno scudo fisico iniziale che assorbe i danni residui.
    *   **Scafo (Health)**: L'integrità strutturale interna (Max 1000).
*   **Danni ai Sistemi**: I colpi che penetrano la corazza hanno il **15% di probabilità** di danneggiare i motori nemici (`engine_health`).
*   **Disattivazione Motori**: Se l'integrità dello scafo di una nave NPC scende sotto il **50% (500 HP)**, i suoi motori vengono disattivati permanentemente. La nave rimarrà immobile, facilitando le manovre di abbordaggio (`bor`).
*   **Portata di Ingaggio**: Le navi ostili apriranno automaticamente il fuoco se un giocatore entra in un raggio di **6.0 unità** (Settore).
*   **Cadenza di Fuoco**: Circa un colpo ogni 5 secondi.
*   **Tattiche**: Le navi NPC non usano siluri al plasma. La loro strategia principale consiste nell'approccio diretto (`cha`) o nella fuga se l'energia scende sotto i livelli critici.

### ☄️ Dinamiche dei Siluri al plasma (Revisione 2026.02)
I siluri (comando `tor`) sono armi simulate fisicamente con alta precisione:
*   **Velocità**: 0.45 unità/tick (incrementata per superare la velocità di crociera delle navi).
*   **Collisione**: I siluri devono colpire fisicamente il bersaglio (distanza **< 0.8**) per esplodere.
*   **Modalità Boresight**: Anche senza un `lock` attivo, il siluro può colpire NPC o altri giocatori se la sua traiettoria interseca il loro raggio di collisione.
*   **Guida**: Se lanciati con un `lock` attivo, i siluri correggono la loro rotta del **35%** per tick verso il bersaglio, influenzato dall'integrità dei **Sensori (ID 2)**.
*   **Overhaul Visivo**: Nucleo geometrico aumentato (0.12), raggi di scarica stellare estesi (0.85) e bagliore di singolarità potenziato per la massima visibilità tattica.
*   **Ostacoli**: I corpi celesti come **Stelle, Pianeti e Buchi Neri** sono oggetti fisici solidi. Un siluro che impatta contro di essi verrà assorbito/distrutto senza colpire il bersaglio dietro di essi. Usa il terreno galattico per coprirti!
*   **Basi Stellari**: Anche le basi stellari bloccano i siluri. Attenzione al fuoco amico o incidentale.
*   **Limiti**: La traiettoria è monitorata su tutti e tre gli assi (X, Y, Z); il siluro si autodistrugge se esce dai confini del settore (0-10) o dopo un timeout di 300 tick.

### 🌪️ Anomalie Spaziali e Rischi Ambientali
Il quadrante è disseminato di fenomeni naturali rilevabili sia dai sensori che dalla **vista tattica 3D**:
*   **Mostri Spaziali (ID 18xxx)**: Entità biologiche ostili (Entità Cristallina, Amoeba Spaziale). Sono estremamente aggressive e possono essere inseguite con il comando `cha`.
*   **Piattaforme di Difesa (ID 16xxx)**: Strutture fisse pesantemente armate che proteggono settori strategici. Possono essere agganciate (`lock`), scansionate (`scan`) e distrutte con fasatori o siluri.
*   **Rift Spaziali (ID 17xxx)**: Distorsioni nel tessuto spazio-temporale.
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
*   **Comete (ID 10xxx)**:
    *   **Effetto**: Oggetti in movimento veloce che attraversano il settore in orbite eccentriche.
    *   **Vista 3D**: Nuclei ghiacciati con una scia blu di gas e polvere.
    *   **Azioni Tattiche**: Possono essere agganciate (`lock`), scansionate (`scan`) e intercettate con l'autopilota (`apr`).
    *   **Raccolta Risorse**: Avvicinarsi alla coda (**Distanza < 0.6**) permette la raccolta automatica di **Gas Nebulare**.
    *   **Strategia**: Usa il comando `cha` (Inseguimento) per sincronizzare la velocità della nave con quella della cometa, facilitando il mantenimento della posizione nella scia per una raccolta ottimale.
*   **Campi di Asteroidi (ID 8xxx)**:
    *   **Effetto**: Ammassi di rocce spaziali di varie dimensioni.
    *   **Vista 3D**: Rocce marroni rotanti con forme irregolari.
    *   **Pericolo**: Navigare all'interno a velocità d'impulso superiore a **0.1** causa danni continui agli scudi. Se gli scudi sono esauriti, l'**Integrità dello Scafo** viene erosa progressivamente. Ridurre la velocità sotto 0.1 per navigare in sicurezza.
*   **Singolarità / Buchi Neri (ID 7xxx)**:
    *   **Slingshot Stress**: Durante una manovra di fionda gravitazionale (`NAV_STATE_SLINGSHOT`), velocità superiori a **1.5** causano stress strutturale con danni periodici allo scafo.
    *   **Raccolta Pericolosa**: L'estrazione di antimateria (`har`) senza protezione degli scudi danneggia direttamente lo scafo a causa delle forze di marea.

### 🛠️ Manutenzione e Riparazioni
**Nota Importante**: L'integrità dello scafo **non si rigenera mai autonomamente**. 
*   **Riparazione sul campo**: Usa il comando `fix` (richiede 50 Grafene e 20 Neo-Ti).
*   **Revisione Totale**: Attracca a una Base Stellare (`doc`) per un ripristino completo e gratuito.
*   **Sistemi**: Solo gli scudi energetici si ricaricano autonomamente (se il reattore ha energia sufficiente e il sistema ID 8 è operativo).
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
    *   **Impatto Tecnico**: Colpire una tempesta causa una **lieve perdita dell'1,5%** della salute dei sensori (ID 2).
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
    <td><img src="readme_assets/actor-khor-khorak-dahar.png" alt="Alto Comando Korthian" width="200"/></td>
  </tr>
</table>

*   **Kor**: Il leggendario "Maestro Dahar", pioniere dei primi contatti tattici con l'Alleanza.
*   **Khorak**: Comandante Supremo delle forze Korthian durante la Grande Guerra Galattica.
*   **Dahar**: Cancelliere e veterano della Guerra Civile Korthian.

#### 2. Impero Stellare Xylari
<table>
<tr>
    <td><img src="readme_assets/gpc-xylari.png" alt="Xylari Star Empire" width="200"/></td>
    <td><img src="readme_assets/actor-alara-xal-selatal.png" alt="Alto Comando Xylario" width="200"/></td>
  </tr>
</table>

*   **Xal'Tar**: Comandante di vascelli di classe D'deridex e storico avversario tattico.
*   **Alara**: Comandante operativo e stratega specializzata in operazioni di infiltrazione.
*   **Sela Tal**: Comandante della Nightshade, celebre per la manipolazione dei confini e l'orchestrazione del blocco silenzioso nel Settore Ombra.

#### 3. Collettivo Swarm
<table>
<tr>
    <td><img src="readme_assets/gpc-swarm.png" alt="Swarm Collective" width="200"/></td>
    <td><img src="readme_assets/actor-nodo-queen-matrix.png" alt="Collettivo Swarm" width="200"/></td>
  </tr>
</table>

*   🤖 **Nodo-Alpha 01**: La prima intelligenza dell'alveare a coordinare l'assimilazione tecnologica di interi sistemi stellari.
*   **La Regina**: Nodo di coordinamento centrale del Collettivo.
*   **Unimatrice 01**: Designazione di comando per i vascelli di classe Diamond o i Cubi Tattici.

#### 4. Unione Vesperiana
<table>
<tr>
    <td><img src="readme_assets/gpc-vesperian.png" alt="Unione Vesperiana" width="200"/></td>
    <td><img src="readme_assets/actor-madred-dukat-damar.png" alt="Unione Vesperiana" width="200"/></td>
  </tr>
</table>

*   **Gul Dukat**: Leader delle forze d'occupazione e comandante della stazione Terok Nor.
*   **Gul Madred**: Esperto in interrogatori e operazioni di intelligence.
*   **Gul Damar**: Leader della resistenza vesperiana e successore al comando supremo.

#### 5. Ascendente (Ascendant)
<table>
<tr>
    <td><img src="readme_assets/gpc-ascendant.png" alt="Ascendant" width="200"/></td>
    <td><img src="readme_assets/actor-ramata-ikatika-karatulan.png" alt="Alto Comando Ascendente" width="200"/></td>
  </tr>
</table>

*   **Remata'Klan**: Primo degli Ascendant, simbolo di disciplina e lealtà assoluta.
*   **Ikat'ika**: Comandante delle forze di terra e maestro del combattimento tattico.
*   **Karat'Ulan**: Comandante operativo nel Quadrante Gamma.

#### 6. Matrice Quarzite
<table>
<tr>
    <td><img src="readme_assets/gpc-quarzite-matrix.png" alt="Quarzite Matrix" width="200"/></td>
    <td><img src="readme_assets/actor-loskene-terev-sthross.png" alt="Matrice Quarzite" width="200"/></td>
  </tr>
</table>

*   **Loskene**: Comandante noto per l'impiego della rete energetica Quarzite.
*   **Terev**: Ambasciatore e comandante coinvolto in dispute territoriali.
*   **Sthross**: Comandante di flottiglia esperto in tattiche di confinamento energetico.

#### 7. Legione Sauriana
<table>
<tr>
    <td><img src="readme_assets/gpc-saurian.png" alt="Saurian Legion" width="200"/></td>
    <td><img src="readme_assets/actor-slar-sless-varn.png" alt="Legione Sauriana" width="200"/></td>
  </tr>
</table>

*   **Slar**: Comandante guerriero attivo durante le prime fasi di espansione.
*   **S'Sless**: Capitano incaricato della difesa degli avamposti di frontiera.
*   **Varn**: Comandante della flotta durante le scaramucce nel Quadrante Alfa.

#### 8. Cartello Dorato
<table>
<tr>
    <td><img src="readme_assets/gpc-guilded.png" alt="Gilded Cartel" width="200"/></td>
    <td><img src="readme_assets/actor-tog-bok-goss.png" alt="Cartello Dorato" width="200"/></td>
  </tr>
</table>

*   **DaiMon Bok**: Noto per l'impiego di tecnologie di simulazione e vendette personali.
*   **DaiMon Tog**: Comandante specializzato in acquisizioni tecnologiche forzate.
*   **DaiMon Goss**: Rappresentante tattico durante i negoziati per il controllo del Wormhole.

#### 9. Vuoto Fluidico (Fluidic Void)
<table>
<tr>
    <td><img src="readme_assets/gpc-fluidic.png" alt="Fluidic Void" width="200"/></td>
    <td><img src="readme_assets/actor-boothby-archer-nodo.png" alt="Vuoto Fluidico" width="200"/></td>
  </tr>
</table>

*   **Boothby (Impostore)**: Entità dedicata all'infiltrazione e allo studio del comando della Flotta.
*   **Bio-Nave Alpha**: Designazione del coordinatore tattico dei vascelli organici.
*   **Lyrerie Archer (Impostore)**: Soggetto di infiltrazione per missioni di ricognizione profonda.

#### 10. Enclave Cryos
<table>
<tr>
    <td><img src="readme_assets/gpc-cryos.png" alt="Cryos Enclave" width="200"/></td>
    <td><img src="readme_assets/actor-pran-archon-tarex.png" alt="Enclave Cryos" width="200"/></td>
  </tr>
</table>

*   **Thot Pran**: Comandante di alto rango durante l'offensiva nel Quadrante Alfa.
*   **Archon**: Leader operativo durante l'alleanza strategica con gli Ascendant.
*   **Thot Tarek**: Comandante delle forze d'attacco Cryos.

#### 11. Apex
<table>
<tr>
    <td><img src="readme_assets/gpc-apex.png" alt="Apex" width="200"/></td>
    <td><img src="readme_assets/actor-idrin-karr-turanj.png" alt="Apex" width="200"/></td>
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

🌌 **"Sidera Jungit Sapientia"**
*(La saggezza unisce le stelle)*
* **Perché rispecchia l'Alleanza**: Questo motto suggerisce che non è solo la forza militare o la tecnologia a tenere unita la galassia, ma la conoscenza condivisa e la comprensione intellettuale tra diverse civiltà. È perfetto per un'alleanza che valorizza la scienza e il coordinamento tattico.

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
    > **"Per Tenebras, Lumen"**
    > *(Attraverso le tenebre, la luce)*
    >
    > *   **Il Significato Strategico**: Questo motto incarna la missione esistenziale dell'Alleanza: la trasformazione dell'ignoto in conosciuto. Rappresenta la determinazione del comando nel penetrare i quadranti inesplorati e i settori più oscuri dello spazio non per conquistare, ma per illuminare.
    > *   **La Dualità Tenebre/Luce**: Le "tenebre" non sono solo il vuoto fisico dello spazio profondo o la minaccia di civiltà ostili, ma simboleggiano l'entropia, l'ignoranza e il caos. La "luce" è la scienza, l'ordine tattico garantito dal sistema GDIS e la civiltà che l'Alleanza porta con sé.
    > *   **Riferimento Tecnologico**: In un contesto di ingegneria navale, il motto riflette la resilienza dei sistemi: la capacità di un vascello (come la classe Deep Space Vanguard) di operare in condizioni di isolamento estremo, mantenendo accesa la "luce" della ragione e della funzionalità tecnologica anche di fronte alle sfide più cupe dell'universo.
    >
    > È il testamento spirituale di chiunque serva a bordo di una nave dell'Alleanza: la convinzione che, per quanto vasto sia il buio, la volontà umana sostenuta dalla conoscenza possa sempre trovare la via.
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

*   📜 **Comandante Leandros Thorne, Sr.**: Un raffinato diplomatico e tattico, famoso per il Trattato di Aetherium che pose fine alla guerra centenaria con i Korthian.

#### ⚔️ 2. Impero Korthian
<table>
<tr>
    <td><img src="readme_assets/arald-korthian.png" alt="Emblem" width="200"/></td>
 </tr>
</table>

*   🩸 **Signore della Guerra Khorak**: Il tattico più brutale dell'impero, famoso per la sua dottrina del "Fuoco Perpetuo" e la conquista del Settore Nero.
*   🗡️ **Generale Valkar**: Un leggendario comandante che unificò le casate in guerra sotto un unico vessillo di conquista galattica.

#### 🎭 3. Impero Stellare Xylari
<table>
<tr>
    <td><img src="readme_assets/arald-xylari.png" alt="Emblem" width="200"/></td>
 </tr>
</table>

*   🐍 **Gran Pretore Nyx**: Maestro di furtività e sabotaggio, svanì per dieci anni prima di riemergere con una flotta fantasma nel cuore del territorio nemico.
*   👁️ **Inquisitore Malakor**: Il primo a utilizzare le frequenze di crittografia Camellia per manipolare i flussi di dati dei sensori nemici.

#### 🕸️ 4. Collettivo Swarm
<table>
<tr>
    <td><img src="readme_assets/arald-swarm.png" alt="Emblem" width="200"/></td>
 </tr>
</table>

*   🤖 **Nodo-Alpha 01**: La prima intelligenza dell'alveare a coordinare l'assimilazione tecnologica di interi sistemi stellari.
*   🔗 **Unity Prime**: Un'entità biomeccanica incaricata di ottimizzare il consumo di massa stellare nelle nebulose energetiche.

#### 🏛️ 5. Unione Vesperiana
<table>
<tr>
    <td><img src="readme_assets/arald-vesperian.png" alt="Emblem" width="200"/></td>
 </tr>
</table>

*   📐 **Legato Thrax**: Architetto della difesa galattica, noto per aver trasformato semplici asteroidi in fortezze inespugnabili.

#### 🔮 6. L'Ascendenza (The Ascendancy)
<table>
<tr>
    <td><img src="readme_assets/arald-ascendancy.png" alt="Emblem" width="200"/></td>
 </tr>
</table>

*   🛐 **Primo Arconte Voth**: La guida spirituale e militare che guidò la sua flotta "Ascendente" attraverso il Grande Vuoto.

#### 💎 7. Matrice Quarzite
<table>
<tr>
    <td><img src="readme_assets/arald-quarzite.png" alt="Emblem" width="200"/></td>
 </tr>
</table>

*   💠 **Rifrazione Zero**: Un'entità cristallina pura capace di calcolare rotte iperspaziali a una velocità superiore a qualsiasi computer biologico.

#### 💎 8. Cartello Dorato
<table>
<tr>
    <td><img src="readme_assets/arald-gilded.png" alt="Emblem" width="200"/></td>
 </tr>
</table>

*   📈 **Barone Silas**: Il magnate che monopolizzò il commercio di Void-Essence e Aetherium attraverso tre quadranti.

#### ❄️ 9. Enclave Cryos
<table>
<tr>
    <td><img src="readme_assets/arald-enclave.png" alt="Emblem" width="200"/></td>
 </tr>
</table>

*   🧊 **Custode Boreas**: Governatore delle distese ghiacciate, esperto in tattiche di guerriglia termica e soppressione del segnale.

#### 🎯 10. Apex Stalker
<table>
<tr>
    <td><img src="readme_assets/arald-apex.png" alt="Emblem" width="200"/></td>
 </tr>
</table>

*   🏹 **Alpha Hunter Kael**: Noto come "Il Fantasma del Settore Zero", rinomato per non aver mai mancato un bersaglio con i suoi siluri al plasma a guida manuale.

---


### 🔵 Profili Operativi per Classe di Vascello (Alleanza)

In Space GL, la scelta della classe del vascello definisce il profilo operativo del Comandante. Di seguito sono riportati i riferimenti per le classi principali:

#### 🏛️ Classe Legacy (Incrociatore Pesante)
<table>
<tr>
    <td><img src="readme_assets/ship-legacy.png" alt="Emblem" width="200"/></td>
 </tr>
</table>

Il simbolo dell'esplorazione dell'Alleanza. Un vascello equilibrato, versatile e robusto.
*   **Comandante di Riferimento**: **Hyperion Niklaus**. La sua leadership sull'Aegis originale ha definito gli standard tattici dell'accademia.

#### 🛡️ Classe Esploratore (Nave Ammiraglia)
<table>
<tr>
    <td><img src="readme_assets/ships-lyra.png" alt="Emblem" width="200"/></td>
 </tr>
</table>

Progettata per missioni di lunga durata e primo contatto. Dispone dei sistemi GDIS più avanzati.
*   **Comandante di Riferimento**: **Lyra Vance**. Eccelleva nell'uso dei sensori a lungo raggio per evitare conflitti non necessari.

#### ⚔️ Classe Nave Ammiraglia (Incrociatore Tattico)
<table>
<tr>
    <td><img src="readme_assets/ship-thorne.png" alt="Emblem" width="200"/></td>
 </tr>
</table>

L'ultima espressione della potenza di fuoco dell'Alleanza, equipaggiata con pesanti banchi Ion Beam.
*   **Comandante di Riferimento**: **Leandros Thorne, Jr**. Famoso per l'uso coordinato di scudi localizzati e salve di siluri al plasma.

#### 🔭 Nave Scientifica (Esploratore Scientifico)
<table>
<tr>
    <td><img src="readme_assets/ship-malakor.png" alt="Emblem" width="200"/></td>
 </tr>
</table>

Un vascello specializzato nell'analisi di anomalie spaziali e nella raccolta di Aetherium.
*   **Comandante di Riferimento**: **Inquisitore Malakor** (Acquisito). Sebbene Xylari, le sue teorie sulla risonanza spaziale sono studiate in ogni missione scientifica.

### 🛠️ Sistemi Tattici e di Navigazione Avanzati

Il progetto implementa soluzioni ingegneristiche all'avanguardia per garantire la superiorità operativa nello spazio profondo:

#### 🚀 1. Autopilota di Avvicinamento (Precision APR)
Il sistema di navigazione assistita (`apr`) opera con alta precisione, garantendo una tolleranza di arresto di **0.1 unità di settore**.
*   **Arrivo Fluido**: Include una **rampa di decelerazione lineare** entro 1.0 unità dal bersaglio e un **aggancio finale** (snap) per eliminare il tremolio di HUD e bussola all'arrivo.
*   **Finalità**: Questa calibrazione è essenziale per le operazioni di **Boarding (`bor`)** e **Docking**, che richiedono il posizionamento del vascello entro raggi d'azione estremamente ridotti (< 1.0 unità).
*   **Versatilità**: Il sistema di puntamento è universale e garantisce la stessa precisione verso navi di qualsiasi fazione (Quarzite, Korthian, Swarm), basi stellari o anomalie galattiche.

#### 💣 2. Sistema Torpedini Multi-Tubo (4-Tube Rotary System)
L'architettura tattica del vascello prevede ora **4 tubi lancia-torpedini indipendenti** a rotazione automatica.
*   **Cadenza di Fuoco**: Il sistema permette di lanciare fino a 4 siluri in rapida successione prima di saturare i buffer di caricamento.
*   **Ciclo di Ricarica**: Ogni tubo opera su un timer di ricarica indipendente di **3 secondi** (90 tick del server).
*   **Interfaccia HUD**: Lo stato di ogni singolo tubo è monitorato in tempo reale sul visore 3D tramite i codici: `[R]` (Pronto), `[L]` (In ricarica), `[F]` (In lancio).

---

#### 🛠️ Altre Classi Operative
<table>
<tr>
    <td><img src="readme_assets/ships1.png" alt="Emblem base" width="200"/></td>
    <td><img src="readme_assets/ships2.png" alt="Emblem medium" width="200"/></td>
    <td><img src="readme_assets/ships3.png" alt="Emblem high" width="200"/></td>
    <td><img src="readme_assets/ships4.png" alt="Emblem high" width="200"/></td>
 </tr>
</table>

L'Alleanza impiega anche vascelli specializzati come la classe **Carrier** (coordinamento droni), **Tactical Cruiser** (difesa perimetrale) e **Deep Space Vanguard** (classe Interstellar - proiezione di potenza e missioni a lungo raggio), ciascuno ottimizzato per scenari di crisi specifici. La *Deep Space Vanguard* eccelle in tattiche avanzate, armamenti pesanti e sistemi di propulsione sperimentali, garantendo massima versatilità nei viaggi interstellari prolungati.

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
*   **Tasto F7**: Cambia il livello di **Anisotropic Filtering** (1x - 16x) per la nitidezza delle texture inclinate.
*   **Tasto F8**: Regola la densità dello **Starfield** (1000 - 8000 stelle) per bilanciare resa estetica e prestazioni.
*   **Tasto ESC**: Chiude il visore 3D.

### 🖥️ Prestazioni Visive e Rendering (Revisione 2026.02)
Il visore 3D è stato ottimizzato per monitor moderni:
*   **Risoluzione Nativa**: Supporto Full HD (**1920x1080**) tramite definizioni macro (`TACTICAL_CUBE_W/H`).
*   **HUD Multi-Tubo**: Visualizzazione in tempo reale degli stati dei 4 tubi di lancio (`[R]` Ready, `[L]` Loading, `[F]` Firing).
*   **GFX Status**: Una riga telemetrica dedicata mostra i parametri attuali di filtraggio e densità stellare.
*   **Scambio Fuoco Condiviso**: Sia i siluri che i fasci dei fasatori (Ion Beams) sono ora oggetti fisici sincronizzati; ogni lampo di energia nel quadrante è visibile a tutti i giocatori, permettendo l'osservazione di battaglie remote.
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
Per compilare ed eseguire la suite SPACE GL, assicurati che siano installate le seguenti librerie:
*   **FreeGLUT / OpenGL**: Motore di rendering principale e gestione delle finestre.
*   **GLEW**: OpenGL Extension Wrangler per il supporto avanzato agli shader.
*   **OpenSSL**: Richiesto per la suite crittografica completa (AES, HMAC, ecc.).
*   **POSIX Threads & RT**: Gestiti tramite `lpthread` e `lrt` per la memoria condivisa e la sincronizzazione.

### 🚀 Zero-Latency IPC Architecture

L'architettura di sistema è progettata per eliminare il collo di bottiglia del networking locale, tipico delle soluzioni client-server tradizionali, garantendo una latenza deterministica prossima allo zero.

#### 🧠 1. Shared Memory Foundation (`/dev/shm`)
Invece di affidarsi a socket TCP/UDP o Named Pipes (che richiedono molteplici context switch tra kernel e user space), il sistema utilizza la **memoria condivisa POSIX**.
*   **Mappatura**: Il Client crea un oggetto di memoria in `/dev/shm` (un file system in RAM) tramite `shm_open`.
*   **Indirizzamento**: Entrambi i processi mappano lo stesso segmento fisico nei propri spazi di indirizzamento virtuali tramite `mmap()`. 
*   **Vantaggio**: Una volta stabilita la mappatura, lo scambio di dati avviene a **velocità di bus della RAM**, senza overhead di sistema operativo per il transito dei byte.

#### 🔄 2. Sincronizzazione Inter-Processo (`PTHREAD_SHARED`)
La coerenza dei dati tra il thread di rete del Client e il loop di rendering del Visualizzatore è gestita tramite primitive di sincronizzazione atomiche allocate direttamente nella memoria condivisa:
*   **Mutex Process-Shared**: Utilizzo di `pthread_mutex_t` inizializzati con l'attributo `PTHREAD_PROCESS_SHARED`. Questo permette di bloccare l'accesso alla struttura `GameState` in modo sicuro tra processi diversi.
*   **Semafori POSIX**: Un `sem_t` viene utilizzato per implementare un meccanismo di *Signaling* a bassa latenza. Quando il Client riceve un pacchetto dal server remoto, aggiorna la memoria e incrementa il semaforo, svegliando istantaneamente il thread `shm_listener` del motore 3D.

#### ⚡ 3. Meccanismo Zero-Copy
A differenza delle architetture basate su messaggi (dove i dati vengono serializzati, copiati in un buffer, inviati e deserializzati), SPACE GL implementa una vera filosofia **Zero-Copy**:
*   **Aggiornamento sul posto**: I dati ricevuti dal network vengono scritti direttamente nella struttura `SharedObject` puntata da `g_shared_state`.
*   **Accesso Diretto**: Il motore grafico legge i valori necessari (coordinate, stati degli scudi, vettori di movimento) accedendo direttamente ai puntatori della memoria condivisa, eliminando qualsiasi operazione di bufferizzazione intermedia.

#### 🛡️ 4. Orchestrazione e Resilienza del Ciclo di Vita
Il Client binario agisce come **Supervisore (Orchestrator)** del ciclo di vita IPC:
1.  **Init**: Genera un path univoco (es. `/st_shm_[PID]`), alloca lo spazio con `ftruncate` e inizializza i mutex.
2.  **Spawn**: Lancia il Visualizzatore passandogli l'identificativo del segmento come argomento.
3.  **Cleanup**: In caso di chiusura o crash intercettato tramite segnali (`SIGINT`, `SIGTERM`), il Client invoca `shm_unlink`. Questo assicura che il segmento di memoria venga rimosso dal sistema, evitando "memory orphans" in `/dev/shm`.

***

**Stack Tecnologico IPC:**
*   **API**: POSIX Real-time Extensions (librt).
*   **Strutture Dati**: `GameState` con `#pragma pack(1)` per garantire il binary alignment identico tra compilazioni diverse.
*   **Latenza Misurata**: < 100 microsecondi per il passaggio di stato tra logica e rendering.

---
*SPACE GL - 3D LOGIC ENGINE. Sviluppato con eccellenza tecnica da Nicola Taibi. "Per Tenebras, Lumen"*
