enum NavigationDashboard {
  struct ViewModel: Equatable {
    var transportOptions: [MWMRouterType]
    var routePoints: RoutePoints
    var routerType: MWMRouterType
    var entity: MWMNavigationDashboardEntity
    var routingOptions: RoutingOptions
    var elevationInfo: ElevationInfo?
    var navigationInfo: NavigationInfo
    var estimates: NSAttributedString
    var dashboardState: MWMNavigationDashboardState
    var presentationStep: NavigationDashboardModalPresentationStep
    var progress: CGFloat
    var navigationSearchState: NavigationSearchState?
    var canSaveRouteAsTrack: Bool
    var errorMessage: String?
  }

  struct ElevationInfo: Equatable {
    var estimates: NSAttributedString
    var image: UIImage?
  }

  struct NavigationInfo: Equatable {
    var state: MWMNavigationInfoViewState
    var availableArea: CGRect
  }
}

extension NavigationDashboard.ViewModel {
  static var initial: Self {
    NavigationDashboard.ViewModel(
      transportOptions: MWMRouterType.allCases,
      routePoints: .empty,
      routerType: MWMRouter.type(),
      entity: MWMNavigationDashboardEntity(),
      routingOptions: RoutingOptions(),
      elevationInfo: nil,
      navigationInfo: .hidden,
      estimates: NSAttributedString(),
      dashboardState: .hidden,
      presentationStep: .hidden,
      progress: 0,
      navigationSearchState: nil,
      canSaveRouteAsTrack: false,
      errorMessage: nil
    )
  }

  var isBottomActionsMenuHidden: Bool { presentationStep == .hidden }

  var startButtonState: StartRouteButton.State {
    if routePoints.count < 2 ||
      routePoints.start == nil ||
      routePoints.finish == nil ||
      dashboardState == .error {
      return .disabled
    }
    if progress < 1 {
      return .loading
    }
    if routerType == .ruler ||
      routerType == .publicTransport {
      return .disabled
    }
    return .enabled
  }

  var estimatesState: EstimatesView.State {
    if dashboardState == .error, let errorMessage {
      return .error(errorMessage)
    }
    if progress < 1 {
      return .loading
    }
    return .estimates(estimates)
  }

  /// Traffic deviation state for the route preview.
  /// Only visible for vehicle routing when usual time data is available.
  var trafficDeviationState: TrafficDeviationView.State {
    guard routerType == .vehicle,
          progress >= 1,
          dashboardState != .error else {
      return .hidden
    }
    let usualTime = Int(entity.usualTimeToTarget)
    let deviation = TrafficDeviationView.DeviationType(rawValue: Int(entity.trafficDeviationType)) ?? .normal
    return TrafficDeviationView.State(usualTimeSeconds: usualTime, deviationType: deviation)
  }
}

extension NavigationDashboard.NavigationInfo {
  static let hidden = NavigationDashboard.NavigationInfo(
    state: .hidden,
    availableArea: MapViewController.shared()?.navigationInfoArea.areaFrame ?? .screenBounds
  )
}

extension CGRect {
  static var screenBounds: CGRect {
    UIScreen.main.bounds
  }
}

extension RoutingOptions {
  var enabledOptionsCount: Int {
    [avoidToll, avoidDirty, avoidFerry, avoidMotorway].filter { $0 }.count
  }
}
