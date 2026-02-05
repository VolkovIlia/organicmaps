/// View displaying traffic deviation info ("usually X min" and "Faster/Slower than usual" badge).
final class TrafficDeviationView: UIView {
  private enum Constants {
    static let badgeCornerRadius: CGFloat = 4
    static let badgePadding = UIEdgeInsets(top: 2, left: 6, bottom: 2, right: 6)
    static let stackSpacing: CGFloat = 8
    static let fasterColor = UIColor(red: 76 / 255.0, green: 175 / 255.0, blue: 80 / 255.0, alpha: 1) // #4CAF50
    static let slowerColor = UIColor(red: 244 / 255.0, green: 67 / 255.0, blue: 54 / 255.0, alpha: 1) // #F44336
  }

  /// Traffic deviation type: 1 = faster, -1 = slower, 0 = normal
  enum DeviationType: Int {
    case faster = 1
    case normal = 0
    case slower = -1
  }

  struct State: Equatable {
    let usualTimeSeconds: Int
    let deviationType: DeviationType

    static let hidden = State(usualTimeSeconds: 0, deviationType: .normal)
  }

  private let stackView: UIStackView = {
    let stack = UIStackView()
    stack.axis = .horizontal
    stack.spacing = Constants.stackSpacing
    stack.alignment = .center
    return stack
  }()

  private let usualTimeLabel: UILabel = {
    let label = UILabel()
    label.font = .regular12()
    label.textColor = .blackSecondaryText()
    return label
  }()

  private let deviationBadge: PaddedLabel = {
    let label = PaddedLabel()
    label.font = .medium11()
    label.textColor = .white
    label.textAlignment = .center
    label.layer.cornerRadius = Constants.badgeCornerRadius
    label.layer.masksToBounds = true
    label.padding = Constants.badgePadding
    return label
  }()

  private var state: State = .hidden

  init() {
    super.init(frame: .zero)
    setupLayout()
  }

  @available(*, unavailable)
  required init?(coder: NSCoder) {
    fatalError("init(coder:) has not been implemented")
  }

  private func setupLayout() {
    addSubview(stackView)
    stackView.addArrangedSubview(usualTimeLabel)
    stackView.addArrangedSubview(deviationBadge)

    stackView.translatesAutoresizingMaskIntoConstraints = false
    NSLayoutConstraint.activate([
      stackView.leadingAnchor.constraint(equalTo: leadingAnchor),
      stackView.trailingAnchor.constraint(lessThanOrEqualTo: trailingAnchor),
      stackView.topAnchor.constraint(equalTo: topAnchor),
      stackView.bottomAnchor.constraint(equalTo: bottomAnchor),
    ])
  }

  func setState(_ newState: State) {
    guard state != newState else { return }
    state = newState

    if newState.usualTimeSeconds <= 0 {
      isHidden = true
      return
    }

    isHidden = false
    updateUsualTimeLabel(seconds: newState.usualTimeSeconds)
    updateDeviationBadge(type: newState.deviationType)
  }

  private func updateUsualTimeLabel(seconds: Int) {
    let timeString = DurationFormatter.durationString(from: TimeInterval(seconds))
    usualTimeLabel.text = String(format: L("usually_x_minutes"), timeString)
  }

  private func updateDeviationBadge(type: DeviationType) {
    switch type {
    case .faster:
      deviationBadge.isHidden = false
      deviationBadge.text = L("faster_than_usual")
      deviationBadge.backgroundColor = Constants.fasterColor
    case .slower:
      deviationBadge.isHidden = false
      deviationBadge.text = L("slower_than_usual")
      deviationBadge.backgroundColor = Constants.slowerColor
    case .normal:
      deviationBadge.isHidden = true
    }
  }
}

/// UILabel with padding support.
private class PaddedLabel: UILabel {
  var padding = UIEdgeInsets.zero

  override func drawText(in rect: CGRect) {
    super.drawText(in: rect.inset(by: padding))
  }

  override var intrinsicContentSize: CGSize {
    let size = super.intrinsicContentSize
    return CGSize(
      width: size.width + padding.left + padding.right,
      height: size.height + padding.top + padding.bottom
    )
  }
}
