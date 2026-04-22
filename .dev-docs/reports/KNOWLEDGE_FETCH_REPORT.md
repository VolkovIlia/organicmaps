# Knowledge Fetch Report: Decentralized Traffic Data & Privacy-Preserving Systems

**Date**: 2026-02-04
**Request**: Research on decentralized traffic crowdsourcing, privacy-preserving location sharing, mesh networking, and related technologies

---

## 1. Decentralized Traffic Data Collection

### Open Source Projects

| Project | Description | Link |
|---------|-------------|------|
| **Organic Maps** | Privacy-first offline maps, no tracking, OSM-based | [organicmaps.app](https://organicmaps.app) |
| **OsmAnd** | Offline OSM navigation with privacy focus | [osmand.net](https://osmand.net) |
| **SUMO** | Traffic simulation from OSM data | [eclipse.dev/sumo](https://eclipse.dev/sumo) |

### Academic Research

| Paper | Key Contribution | Link |
|-------|-----------------|------|
| **PPTM: Privacy-Preserving Traffic Monitoring** | Homomorphic encryption + super-increasing sequences for speed aggregation | [arXiv:1902.04663](https://arxiv.org/abs/1902.04663) |
| **Large-scale Mixed Traffic Control** | Privacy-preserving crowdsourcing + dynamic vehicle routing | [arXiv:2311.11347](https://arxiv.org/abs/2311.11347) |
| **VPTS** | Crowdsourcing traffic monitoring with HE + DP | [arXiv:2508.01798](https://arxiv.org/html/2508.01798) |
| **Nexagon Security** | Secure crowdsourcing of georeferenced data in ITS | [arXiv:2506.05601](https://arxiv.org/html/2506.05601) |

### Key Insight
No mature open-source P2P traffic sharing app exists. Most solutions are either:
- Centralized (Waze, Google Maps)
- Privacy-focused but no real-time traffic (Organic Maps, OsmAnd)
- Academic prototypes not production-ready

---

## 2. Privacy-Preserving Location Sharing

### Differential Privacy Implementations

| Implementation | Technology | Use Case | Source |
|---------------|------------|----------|--------|
| **Google Differential Privacy** | Laplace mechanism | Historical traffic stats (since 2015) | [Brookings Article](https://www.brookings.edu/articles/using-differential-privacy-to-harness-big-data-and-preserve-privacy/) |
| **Uber Elastic Sensitivity** | SQL-compatible DP | Traffic patterns, driver earnings | UC Berkeley research |
| **Transit Smart Card DP** | Time-series DP | Public transit data release | [ScienceDirect](https://www.sciencedirect.com/science/article/abs/pii/S0968090X19310630) |

### Academic Papers

| Paper | Key Innovation | Link |
|-------|---------------|------|
| **Geo-Indistinguishability** | DP definition for location-based systems | [Polytechnique PDF](https://www.lix.polytechnique.fr/~catuscia/papers/Geolocation/geo.pdf) |
| **LDP for Vehicular Networks** | Localized DP with Hidden Markov Model | [Frontiers](https://www.frontiersin.org/journals/physics/articles/10.3389/fphy.2025.1624955/full) |
| **Temporal Correlations DP** | Handling time-correlated location data | [ACM CCS 2015](https://dl.acm.org/doi/10.1145/2810103.2813640) |

### GitHub Repositories

| Repository | Description | Stars |
|------------|-------------|-------|
| [google/differential-privacy](https://github.com/google/differential-privacy) | Google's DP library (C++, Go, Java) | 3k+ |
| [OpenDP](https://github.com/opendp/opendp) | Community DP library | 400+ |

### Key Trade-off
- High privacy (epsilon < 1) = low data utility
- Low privacy (epsilon > 10) = near-original data
- Location data particularly challenging due to temporal correlations

---

## 3. Smartphone Mesh Networking SDKs

### Production-Ready SDKs

| SDK | Platforms | Technology | License | Link |
|-----|-----------|------------|---------|------|
| **Bridgefy** | iOS, Android, Flutter, React Native | BLE mesh, Signal Protocol encryption | Commercial | [bridgefy.me](https://bridgefy.me) |
| **Nordic nRF Mesh** | Android, iOS | BLE Mesh (Bluetooth SIG standard) | BSD 3-Clause | [GitHub](https://github.com/NordicSemiconductor/Android-nRF-Mesh-Library) |
| **Meshrabiya** | Android | WiFi Direct + Local Hotspot mesh | Open Source | [GitHub](https://github.com/UstadMobile/Meshrabiya) |

### Open Source Libraries

| Library | Technology | Platform | Link |
|---------|------------|----------|------|
| **Underdark** | BLE + WiFi | Android | [GitHub](https://github.com/udark/underdark-android) |
| **BLEMeshChat** | BLE 4.0 mesh | iOS, Android | [GitHub](https://github.com/chrisballinger/BLEMeshChat) |
| **Briar** | BLE point-to-point (NOT mesh) | Android | [briarproject.org](https://briarproject.org) |

### Technical Characteristics

**Bridgefy SDK Features**:
- Transmission modes: Mesh (targeted) and Broadcast (flood)
- Propagation profiles: Standard, HighDensity, Sparse, LongReach, ShortReach
- Encryption: Signal Protocol (end-to-end)
- Range: ~100m BLE, multi-hop extends range

**Meshrabiya Features**:
- WiFi Direct + Local Only Hotspots
- TCP/UDP socket factories
- Multi-hop routing
- OkHttp integration

### Critical Security Note
Bridgefy had vulnerabilities exposed in 2021 paper "Breaking Bridgefy" ([IACR ePrint](https://eprint.iacr.org/2021/214.pdf)). They subsequently implemented Signal Protocol.

---

## 4. VANET and V2V Research

### Protocol Standards

| Standard | Region | Frequency | Description |
|----------|--------|-----------|-------------|
| **IEEE 802.11p / WAVE** | USA | 5.9 GHz | Wireless Access in Vehicular Environments |
| **IEEE 1609** | USA | 5.9 GHz | WAVE protocol stack |
| **ETSI ITS-G5** | Europe | 5.9 GHz | European V2X standard |
| **C-V2X** | Global | LTE/5G | Cellular V2X |

### Data Dissemination Protocols

| Protocol | Approach | Key Feature | Source |
|----------|----------|-------------|--------|
| **OAddP** | Whale Optimization clustering | Adaptive overhead reduction | [IEEE](https://ieeexplore.ieee.org/document/8776703/) |
| **DABFS** | BFS-based routing | Warning message dissemination | [ScienceDirect](https://www.sciencedirect.com/science/article/abs/pii/S0140366419304761) |
| **PSO-based** | Particle Swarm Optimization | Efficient relay selection | [PMC](https://pmc.ncbi.nlm.nih.gov/articles/PMC9964182/) |

### Challenges Addressed
- **Broadcast Storm Problem**: Too many vehicles broadcasting simultaneously
- **Intermittent Links**: Vehicles moving in/out of range
- **Sparse Networks**: Rural areas with few vehicles

### Comprehensive Survey
[Springer: VANET Review 2024](https://link.springer.com/article/10.1007/s10922-024-09853-5) - Covers traffic, safety, and remote monitoring applications

---

## 5. COVID Exposure Notification Architecture (GAEN/DP-3T)

### Architecture Overview

```
+------------------+     +------------------+     +------------------+
|   User Device    |     |   User Device    |     |  Health Server   |
|                  |     |                  |     |                  |
| - Generate daily |     | - Generate daily |     | - Store infected |
|   random keys    |     |   random keys    |     |   keys ONLY      |
| - Broadcast BLE  |<--->| - Broadcast BLE  |<--->| - No location    |
|   identifiers    |     |   identifiers    |     |   data           |
| - Store received |     | - Store received |     | - No contact     |
|   identifiers    |     |   identifiers    |     |   logs           |
+------------------+     +------------------+     +------------------+
```

### Key Privacy Properties

1. **Decentralized contact logs**: Never leave device
2. **Rolling identifiers**: Change every 15-20 minutes + MAC rotation
3. **No location data**: Only proximity (BLE signal strength)
4. **User consent**: Explicit opt-in required
5. **Limited retention**: 14-day key rotation

### GitHub Repositories

| Repository | Description | Link |
|------------|-------------|------|
| **DP-3T Documents** | Protocol specification | [GitHub](https://github.com/DP-3T/documents) |
| **DP-3T Backend** | Server implementation | [GitHub](https://github.com/DP-3T/dp3t-sdk-backend) |
| **DP-3T Android SDK** | Android client | [GitHub](https://github.com/DP-3T/dp3t-sdk-android) |
| **SwissCovid iOS** | Production app | [GitHub](https://github.com/DP-3T/dp3t-app-ios-ch) |
| **RadarCOVID** | Spain's app | [GitHub](https://github.com/RadarCOVID) |
| **STAYAWAY COVID** | Portugal (React Native) | [GitHub](https://github.com/stayawayinesctec/stayaway-app) |

### Applicability to Traffic

**Directly Applicable**:
- Rolling pseudonymous identifiers (prevent tracking)
- BLE-based proximity detection
- Decentralized architecture
- 14-day data retention model

**NOT Applicable**:
- Traffic needs location (routes), GAEN explicitly avoids it
- Traffic needs aggregation, GAEN only does binary matching
- Traffic benefits from continuous sharing, GAEN only on infection

### Recommended Hybrid Approach
Combine GAEN-style privacy with aggregated traffic:
1. Use rolling identifiers (like GAEN)
2. Aggregate locally before sharing (unlike GAEN)
3. Add differential privacy noise to aggregates
4. Share only at road-segment granularity, not GPS coordinates

---

## 6. Federated Learning for Traffic Prediction

### GitHub Repositories

| Repository | Focus | Link |
|------------|-------|------|
| **vperifan/Federated-Time-Series-Forecasting** | 5G traffic forecasting | [GitHub](https://github.com/vperifan/Federated-Time-Series-Forecasting) |
| **jwwthu/GNN4Traffic** | Graph Neural Networks for traffic | [GitHub](https://github.com/jwwthu/GNN4Traffic) |
| **FedML-AI/FedML** | Scalable FL framework | [GitHub](https://github.com/FedML-AI/FedML) |
| **rruisong/Awesome-FL-for-AD** | FL for Autonomous Driving | [GitHub](https://github.com/rruisong/Awesome-Federated-Learning-for-Autonomous-Driving) |
| **aptx1231/Traffic-Prediction-Open-Code** | Open code summary | [GitHub](https://github.com/aptx1231/Traffic-Prediction-Open-Code-Summary) |

### Key Papers

| Paper | Method | Accuracy | Link |
|-------|--------|----------|------|
| **FedAGCN** | Async Graph Convolutional Network | -6.85% RMSE, -20.45% MAPE | [Applied Soft Computing](https://www.sciencedirect.com/science/article/pii/S2590005625000384) |
| **MOFL** | Multi-Objective FL + RL | 97.45% | [PeerJ](https://peerj.com/articles/cs-2922/) |
| **FedNET** | LSTM + FedAvg for optical networks | Multi-step prediction | [arXiv](https://arxiv.org/html/2511.06797v1) |
| **FedFlow** | Personalized FL for passenger flow | Privacy-preserving | [Springer](https://link.springer.com/article/10.1007/s10994-025-06795-0) |

### Advantages for Traffic
- Data stays on device
- No central server sees raw GPS
- Model improvements benefit all participants
- Works with heterogeneous data distributions

---

## 7. Delay-Tolerant Networking (DTN)

### Implementations

| Project | Language | Protocol Version | Link |
|---------|----------|-----------------|------|
| **DTN2** | C++ | BP6 | [GitHub](https://github.com/delay-tolerant-networking/DTN2) |
| **DTN7-go** | Go | BP7 (RFC 9171) | [GitHub](https://github.com/dtn7/dtn7-go) |
| **DTN7-rs** | Rust | BP7 | [dtn7.github.io](https://dtn7.github.io/) |
| **ION-DTN** | C | BP7 | NASA JPL |
| **IBR-DTN** | C++ | Modular | [ibr.cs.tu-bs.de](https://www.ibr.cs.tu-bs.de/projects/ibr-dtn/) |

### Curated Resources
[awesome-dtn](https://github.com/dtn7/awesome-dtn) - Comprehensive list of DTN implementations and research

### VDTN (Vehicular DTN) Concepts
- Store-carry-forward paradigm
- Useful for sparse networks
- Vehicles act as data mules
- Higher latency but guaranteed delivery

---

## 8. Synthesis: Recommended Architecture for Organic Maps

### Design Principles (from research)

1. **Privacy-First** (from GAEN/DP-3T):
   - Rolling pseudonymous identifiers
   - No raw GPS sharing
   - Local-first processing

2. **Aggregation** (from DP research):
   - Road-segment level (H3 hexagons)
   - Differential privacy noise
   - Time-windowed aggregates only

3. **Communication** (from mesh SDKs):
   - BLE for discovery
   - WiFi Direct for data transfer
   - Multi-hop when possible

4. **Data Model** (from VANET research):
   - Timestamp + road_segment_id + speed_bucket + count
   - 5-minute aggregation windows
   - 1-hour maximum retention

### Suggested Technology Stack

```
Layer           | Technology           | Open Source Option
----------------|---------------------|-------------------
BLE Mesh        | Nordic nRF Mesh     | BSD-3
WiFi Mesh       | Meshrabiya          | Open Source
Privacy         | OpenDP              | MIT
Aggregation     | H3 (Uber)           | Apache 2.0
Prediction      | FedML               | Apache 2.0
Protocol        | Custom (BP7-like)   | -
```

### Similar Projects to Study

1. **DP-3T** - Best privacy architecture reference
2. **Meshrabiya** - Android WiFi mesh implementation
3. **FedML** - Federated learning framework
4. **H3** - Hexagonal spatial indexing

---

## Sources

### GitHub Repositories
- [DP-3T/documents](https://github.com/DP-3T/documents)
- [DP-3T/dp3t-sdk-backend](https://github.com/DP-3T/dp3t-sdk-backend)
- [NordicSemiconductor/Android-nRF-Mesh-Library](https://github.com/NordicSemiconductor/Android-nRF-Mesh-Library)
- [UstadMobile/Meshrabiya](https://github.com/UstadMobile/Meshrabiya)
- [udark/underdark-android](https://github.com/udark/underdark-android)
- [chrisballinger/BLEMeshChat](https://github.com/chrisballinger/BLEMeshChat)
- [bridgefy/sdk-android](https://github.com/bridgefy/sdk-android)
- [dtn7/dtn7-go](https://github.com/dtn7/dtn7-go)
- [dtn7/awesome-dtn](https://github.com/dtn7/awesome-dtn)
- [FedML-AI/FedML](https://github.com/FedML-AI/FedML)
- [vperifan/Federated-Time-Series-Forecasting](https://github.com/vperifan/Federated-Time-Series-Forecasting)
- [jwwthu/GNN4Traffic](https://github.com/jwwthu/GNN4Traffic)
- [google/differential-privacy](https://github.com/google/differential-privacy)
- [croqaz/awesome-decentralized](https://github.com/croqaz/awesome-decentralized)
- [moarpepes/awesome-mesh](https://github.com/moarpepes/awesome-mesh)

### Academic Papers (arXiv/IEEE/Springer)
- [arXiv:1902.04663](https://arxiv.org/abs/1902.04663) - Privacy-Preserving Traffic Monitoring
- [arXiv:2311.11347](https://arxiv.org/abs/2311.11347) - Large-scale Mixed Traffic Control
- [arXiv:2508.01798](https://arxiv.org/html/2508.01798) - Privacy-Preserving Computing in Automotive
- [Springer: VANET Review 2024](https://link.springer.com/article/10.1007/s10922-024-09853-5)
- [ScienceDirect: DP Transit Smart Card](https://www.sciencedirect.com/science/article/abs/pii/S0968090X19310630)
- [Frontiers: LDP Vehicular Networks](https://www.frontiersin.org/journals/physics/articles/10.3389/fphy.2025.1624955/full)
- [PeerJ: MOFL Traffic Prediction](https://peerj.com/articles/cs-2922/)

### Technical Documentation
- [Bridgefy SDK Docs](https://docs.bridgefy.me/sdk)
- [Exposure Notification Wikipedia](https://en.wikipedia.org/wiki/Exposure_Notification)
- [DP-3T Wikipedia](https://en.wikipedia.org/wiki/Decentralized_Privacy-Preserving_Proximity_Tracing)
- [Wi-Fi Direct Android](https://source.android.com/docs/core/connect/wifi-direct)

### Security Analysis
- [Breaking Bridgefy (IACR 2021)](https://eprint.iacr.org/2021/214.pdf)
- [GAEN Critique](https://www.researchgate.net/publication/364447562_A_Critique_of_the_Google_Apple_Exposure_Notification_GAEN_Framework)
