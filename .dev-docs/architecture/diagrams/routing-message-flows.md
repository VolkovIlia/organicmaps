# Routing Message Flows and Data Flow Diagrams

**Version**: 1.0
**Date**: 2026-02-03

---

## 1. Phase 1: Turn Cost Model Integration

### 1.1 Turn Cost Calculation Flow

```mermaid
sequenceDiagram
    participant IR as IndexRouter
    participant EE as EdgeEstimator
    participant TCM as TurnCostModel
    participant RSP as RegionalSpeedProfile
    participant VM as VehicleModel

    IR->>EE: CalcSegmentWeight(segment, road, purpose)
    EE->>VM: GetSpeed(segment.IsForward())
    VM->>RSP: GetRegionalSpeed(countryCode, roadClass)
    RSP-->>VM: SpeedKMpH {weight, eta}
    VM-->>EE: SpeedKMpH

    alt Has Turn
        EE->>TCM: CalcTurnCost(params)
        Note over TCM: params contains:<br/>turnAngle, fromRoadClass,<br/>toRoadClass, junctionType

        TCM->>TCM: CalcBaseAngleCost(angle)
        TCM->>TCM: GetJunctionDelay(junctionType)
        TCM->>TCM: CalcRoadClassPenalty(from, to)
        TCM-->>EE: TurnCostResult {totalCostSeconds}
    end

    EE-->>IR: segmentWeight + turnCost
```

### 1.2 Speed Profile Lookup

```mermaid
flowchart TD
    subgraph VehicleModelFactory
        A[GetVehicleModelForCountry] --> B{Country has<br/>custom profile?}
        B -->|Yes| C[Load country profile]
        B -->|No| D{Parent country<br/>has profile?}
        D -->|Yes| E[Load parent profile]
        D -->|No| F[Use default profile]
    end

    subgraph RegionalSpeedProfile
        G[GetSpeed] --> H{Road class<br/>exists?}
        H -->|Yes| I[Return configured speed]
        H -->|No| J[Return default speed]

        K[GetSurfaceFactor] --> L{Surface type<br/>configured?}
        L -->|Yes| M[Return surface factor]
        L -->|No| N[Return 1.0]
    end

    C --> G
    E --> G
    F --> G
```

---

## 2. Phase 2: Corridor-Based Route Following

### 2.1 Off-Route Detection State Machine

```mermaid
stateDiagram-v2
    [*] --> OnRoute

    OnRoute --> Monitoring: deviation > corridorWidth
    Monitoring --> OnRoute: back in corridor
    Monitoring --> Deciding: outside > 10 seconds

    Deciding --> SuggestingReturn: returnDist < 200m AND<br/>turnCount <= 2
    Deciding --> Rerouting: forwardDist <= returnDist * 1.2

    SuggestingReturn --> OnRoute: user complies
    SuggestingReturn --> Rerouting: ignored > 30 seconds

    Rerouting --> OnRoute: new route calculated
    Rerouting --> [*]: route calculation failed

    note right of Monitoring
        Timer starts when
        entering this state
    end note

    note right of Deciding
        Compare costs:
        - Distance to return
        - Distance via alternative
    end note
```

### 2.2 Position Update Processing

```mermaid
sequenceDiagram
    participant App as Application
    participant RS as RoutingSession
    participant CT as CorridorTracker
    participant RD as RerouteDecision
    participant IR as IndexRouter

    App->>RS: OnLocationPositionChanged(gpsInfo)
    RS->>CT: CheckPosition(position, route)

    alt Inside Corridor
        CT-->>RS: {state: OnRoute, isInside: true}
        RS->>RS: UpdatePosition()
        RS-->>App: SessionState::OnRoute
    else Outside Corridor
        CT-->>RS: {state: Monitoring, isInside: false}

        alt Time > 10 seconds
            CT->>CT: TransitionTo(Deciding)
            RS->>RD: Decide(position, route)
            RD->>IR: CalcReturnDistance()
            IR-->>RD: returnDistance
            RD->>IR: CalcForwardDistance()
            IR-->>RD: forwardDistance

            alt Return is cheaper
                RD-->>RS: SuggestReturn
                RS-->>App: UI shows "Return when safe"
            else Forward is acceptable
                RD-->>RS: CalculateNewRoute
                RS->>IR: RebuildRoute(position)
                IR-->>RS: newRoute
                RS-->>App: SessionState::OnRoute
            end
        else Time < 10 seconds
            RS-->>App: SessionState::OnRoute (monitoring)
        end
    end
```

### 2.3 Corridor Width Calculation

```mermaid
flowchart LR
    subgraph Input
        A[Road Class]
        B[GPS Accuracy]
    end

    subgraph "Base Width Table"
        C{Road Class}
        C -->|Motorway| D[50m]
        C -->|Trunk| E[40m]
        C -->|Primary| F[30m]
        C -->|Secondary| G[25m]
        C -->|Tertiary| H[20m]
        C -->|Residential| I[15m]
    end

    subgraph Calculation
        J[baseWidth + max(gpsAccuracy, 10m)]
    end

    A --> C
    D --> J
    E --> J
    F --> J
    G --> J
    H --> J
    I --> J
    B --> J

    J --> K[Corridor Width]
```

---

## 3. Phase 3: CCH Preprocessing

### 3.1 CCH Build Pipeline (Generator)

```mermaid
flowchart TD
    subgraph "Map Generation Pipeline"
        A[OSM Data] --> B[Feature Extraction]
        B --> C[Routing Index Generation]
        C --> D[Existing MWM Sections]
    end

    subgraph "CCH Topology Builder (New)"
        E[Road Graph] --> F[Node Ordering]
        F --> G[Nested Dissection]
        G --> H[Shortcut Computation]
        H --> I[Level Assignment]
    end

    subgraph "Output"
        D --> J[MWM File]
        I --> K[CCH_TOPOLOGY Section]
        K --> J
        L[Optional: Default Weights] --> J
    end

    C --> E
```

### 3.2 CCH Runtime Flow

```mermaid
sequenceDiagram
    participant App as Application
    participant IR as IndexRouter
    participant CC as CCHCustomizer
    participant CQ as CCHQueryEngine
    participant AS as AStarFallback

    App->>IR: CalculateRoute(origin, destination, profile)

    IR->>CQ: IsCCHAvailable(mwmIds)

    alt CCH Available
        CQ-->>IR: true

        alt Profile Changed
            IR->>CC: Customize(profile)
            Note over CC: Load topology from MWM<br/>Compute edge weights<br/>~30-60 seconds
            CC-->>IR: customizationComplete
        end

        IR->>CQ: Query(origin, destination)
        Note over CQ: Bidirectional Dijkstra<br/>on contracted graph<br/>~1-10ms
        CQ->>CQ: UnpackPath()
        CQ-->>IR: CCHQueryResult {route, queryTimeMs}

    else CCH Not Available
        CQ-->>IR: false
        IR->>AS: FindPathBidirectional(params)
        Note over AS: Standard A* algorithm<br/>~100-500ms
        AS-->>IR: RoutingResult
    end

    IR-->>App: Route
```

### 3.3 CCH Data Structures

```mermaid
classDiagram
    class CCHTopology {
        +uint32_t version
        +uint64_t nodeCount
        +uint64_t edgeCount
        +vector~NodeOrder~ nodeOrder
        +vector~Shortcut~ shortcuts
        +vector~uint64_t~ levelOffsets
        +Deserialize(reader)
        +GetLevel(nodeId) uint32_t
    }

    class NodeOrder {
        +uint32_t originalId
        +uint32_t contractedId
        +uint32_t level
    }

    class Shortcut {
        +uint32_t fromNode
        +uint32_t toNode
        +uint32_t middleNode
        +bool isForward
    }

    class CCHCustomizer {
        -CCHTopology* topology
        -vector~double~ weights
        -CCHProfile currentProfile
        +Customize(profile, vehicleModel)
        +GetWeight(shortcutId) double
    }

    class CCHQueryEngine {
        -CCHTopology* topology
        -CCHCustomizer* customizer
        +Query(origin, destination) CCHQueryResult
        +UnpackPath(contractedPath) vector~Segment~
    }

    CCHTopology "1" *-- "*" NodeOrder
    CCHTopology "1" *-- "*" Shortcut
    CCHCustomizer --> CCHTopology
    CCHQueryEngine --> CCHTopology
    CCHQueryEngine --> CCHCustomizer
```

---

## 4. Phase 4: Alternative Routes

### 4.1 k-SPwLO Algorithm Flow

```mermaid
flowchart TD
    subgraph "Find Alternatives"
        A[Primary Route P1] --> B{i < k?}
        B -->|Yes| C[Select via_node from<br/>high-importance nodes]
        C --> D[CCH Query: origin -> via_node]
        D --> E[CCH Query: via_node -> destination]
        E --> F[Candidate = path1 + path2]
        F --> G{overlap < threshold?}
        G -->|Yes| H{length < 1.3 * P1?}
        H -->|Yes| I[Score = length + penalty * overlap]
        I --> J[Update best_candidate if score < best]
        J --> B
        H -->|No| C
        G -->|No| C
        B -->|No| K[Return paths]
    end

    subgraph "Overlap Calculation"
        L[Route 1 edges] --> M[Set intersection<br/>with Route 2 edges]
        M --> N[overlap = intersection / union]
    end
```

### 4.2 Alternative Route Display Timing

```mermaid
sequenceDiagram
    participant RS as RoutingSession
    participant AF as AlternativeFinder
    participant UI as UserInterface

    Note over RS: Route calculated with alternatives

    loop Every GPS update
        RS->>RS: GetDistanceToNextDecisionPoint()

        alt Distance in [500m, 2km]
            RS->>RS: GetAlternativeAtDecisionPoint()

            alt Alternative saves > 10% time
                RS->>UI: ShowAlternative(alternativeRoute)
                UI-->>RS: userChoice

                alt User selects alternative
                    RS->>RS: SwitchToAlternative(index)
                else User dismisses
                    RS->>RS: MarkDismissed(decisionPointId)
                end
            end

        else Distance < 500m
            RS->>UI: HideAlternatives()
            Note over RS: Too late to show alternatives
        end
    end
```

### 4.3 Alternative Routes Storage

```mermaid
classDiagram
    class RoutingSession {
        -shared_ptr~Route~ m_route
        -vector~AlternativeRoute~ m_alternatives
        -set~DecisionPointId~ m_dismissedPoints
        +OnLocationPositionChanged(info)
        +GetAlternatives() vector~AlternativeRoute~
        +SwitchToAlternative(index)
        +DismissAlternative(decisionPointId)
    }

    class AlternativeRoute {
        +int routeIndex
        +double distanceMeters
        +double durationSeconds
        +double overlapWithPrimary
        +vector~DecisionPoint~ decisionPoints
        +vector~Segment~ path
    }

    class DecisionPoint {
        +ms::LatLon position
        +double distanceFromStart
        +int alternativeIndex
        +double timeSavingSeconds
    }

    RoutingSession "1" *-- "0..3" AlternativeRoute
    AlternativeRoute "1" *-- "*" DecisionPoint
```

---

## 5. Cross-Phase Integration

### 5.1 Complete Route Calculation Flow

```mermaid
flowchart TD
    subgraph "User Request"
        A[Origin + Destination]
    end

    subgraph "Phase 1: Cost Model"
        B[Load Regional Speed Profile]
        C[Configure Turn Cost Model]
    end

    subgraph "Phase 3: CCH Query"
        D{CCH Available?}
        E[CCH Query Engine]
        F[A* Fallback]
    end

    subgraph "Phase 4: Alternatives"
        G{Route > 50km?}
        H[k-SPwLO Algorithm]
        I[Single Route Only]
    end

    subgraph "Phase 2: Navigation"
        J[Initialize Corridor Tracker]
        K[Configure Reroute Decision]
    end

    A --> B
    B --> C
    C --> D
    D -->|Yes| E
    D -->|No| F
    E --> G
    F --> G
    G -->|Yes| H
    G -->|No| I
    H --> J
    I --> J
    J --> K
    K --> L[Navigation Session Active]
```

### 5.2 Component Dependencies

```mermaid
graph TD
    subgraph "Phase 1 (Independent)"
        A[TurnCostModel]
        B[RegionalSpeedProfile]
        C[SurfacePenalty]
    end

    subgraph "Phase 2 (Independent)"
        D[CorridorTracker]
        E[RerouteDecision]
    end

    subgraph "Phase 3 (Depends on Phase 1)"
        F[CCHTopologyBuilder]
        G[CCHCustomizer]
        H[CCHQueryEngine]
    end

    subgraph "Phase 4 (Depends on Phase 3)"
        I[AlternativeFinder]
        J[OverlapCalculator]
    end

    A --> G
    B --> G
    H --> I
    D --> E

    style A fill:#90EE90
    style B fill:#90EE90
    style C fill:#90EE90
    style D fill:#90EE90
    style E fill:#90EE90
    style F fill:#FFD700
    style G fill:#FFD700
    style H fill:#FFD700
    style I fill:#FFA07A
    style J fill:#FFA07A
```

---

## 6. Error Handling and Fallbacks

### 6.1 CCH Fallback Chain

```mermaid
flowchart TD
    A[Route Request] --> B{CCH Topology<br/>in MWM?}
    B -->|Yes| C{CCH Customized<br/>for Profile?}
    B -->|No| G[A* Bidirectional]

    C -->|Yes| D[CCH Query]
    C -->|No| E[Customize CCH]
    E -->|Success| D
    E -->|Failure| G

    D -->|Success| F[Return Route]
    D -->|Timeout| G
    D -->|No Path| G

    G -->|Success| F
    G -->|Failure| H[Return Error]

    style G fill:#FFD700
    style F fill:#90EE90
    style H fill:#FF6347
```

### 6.2 Corridor Recovery

```mermaid
stateDiagram-v2
    [*] --> Normal

    Normal --> GpsLost: accuracy > 100m
    GpsLost --> Normal: accuracy < 50m
    GpsLost --> ExpandCorridor: duration > 30s

    Normal --> TunnelMode: no GPS signal
    TunnelMode --> Normal: GPS restored
    TunnelMode --> DeadReckoning: duration > 60s

    ExpandCorridor --> Normal: back on route
    DeadReckoning --> Normal: GPS restored

    note right of ExpandCorridor
        Double corridor width
        to account for uncertainty
    end note

    note right of DeadReckoning
        Estimate position based
        on last known speed/heading
    end note
```
