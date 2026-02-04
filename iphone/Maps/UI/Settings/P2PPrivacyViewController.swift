import UIKit

/// Privacy Dashboard for P2P traffic sharing.
/// Shows consent controls, status, and privacy information.
final class P2PPrivacyViewController: MWMTableViewController {

  // MARK: - Consent Level

  private enum ConsentLevel: Int {
    case off = 0
    case viewOnly = 1
    case contribute = 2

    var title: String {
      switch self {
      case .off: return L("p2p_consent_off")
      case .viewOnly: return L("p2p_consent_view_only")
      case .contribute: return L("p2p_consent_contribute")
      }
    }
  }

  // MARK: - IBOutlets

  @IBOutlet private weak var consentLevelCell: SettingsTableViewSelectableCell!
  @IBOutlet private weak var peersStatusCell: UITableViewCell!
  @IBOutlet private weak var exchangesStatusCell: UITableViewCell!
  @IBOutlet private weak var whatSharedCell: UITableViewCell!
  @IBOutlet private weak var privacyGuaranteesCell: UITableViewCell!
  @IBOutlet private weak var disableNowCell: UITableViewCell!
  @IBOutlet private weak var debugModeCell: SettingsTableViewSwitchCell!

  // MARK: - Lifecycle

  override func viewDidLoad() {
    super.viewDidLoad()
    title = L("p2p_privacy_title")
    configureStaticCells()
  }

  override func viewWillAppear(_ animated: Bool) {
    super.viewWillAppear(animated)
    updateUI()
  }

  // MARK: - Configuration

  private func configureStaticCells() {
    whatSharedCell.textLabel?.text = L("p2p_what_shared_title")
    whatSharedCell.detailTextLabel?.text = L("p2p_what_shared_summary")
    whatSharedCell.detailTextLabel?.numberOfLines = 0

    privacyGuaranteesCell.textLabel?.text = L("p2p_privacy_guarantees_title")
    privacyGuaranteesCell.detailTextLabel?.text = L("p2p_privacy_guarantees_summary")
    privacyGuaranteesCell.detailTextLabel?.numberOfLines = 0

    disableNowCell.textLabel?.text = L("p2p_disable_now")
    disableNowCell.textLabel?.textColor = .systemRed

    debugModeCell.config(withTitle: L("p2p_debug_mode"))
    debugModeCell.delegate = self
  }

  private func updateUI() {
    let consentManager = MWMP2PConsentManager.shared
    let level = ConsentLevel(rawValue: Int(consentManager.consentLevel)) ?? .off

    // Update consent level cell
    consentLevelCell.config(withTitle: L("p2p_consent_level_title"), info: level.title)

    // Update status cells
    let isActive = level != .off
    if isActive {
      let peerCount = MWMP2PTrafficService.shared.discoveredPeerCount
      if peerCount > 0 {
        peersStatusCell.detailTextLabel?.text = String(format: L("p2p_status_peers_count"), peerCount)
      } else {
        peersStatusCell.detailTextLabel?.text = L("p2p_status_peers_none")
      }
      // TODO: Get exchange count from native layer
      exchangesStatusCell.detailTextLabel?.text = L("p2p_status_exchanges_none")
    } else {
      peersStatusCell.detailTextLabel?.text = L("p2p_status_peers_none")
      exchangesStatusCell.detailTextLabel?.text = L("p2p_status_exchanges_none")
    }

    peersStatusCell.textLabel?.text = L("p2p_status_peers")
    exchangesStatusCell.textLabel?.text = L("p2p_status_exchanges")

    // Update disable button state
    disableNowCell.isUserInteractionEnabled = isActive
    disableNowCell.textLabel?.alpha = isActive ? 1.0 : 0.5
  }

  // MARK: - Table View Delegate

  override func tableView(_ tableView: UITableView, didSelectRowAt indexPath: IndexPath) {
    tableView.deselectRow(at: indexPath, animated: true)

    let cell = tableView.cellForRow(at: indexPath)

    if cell === consentLevelCell {
      showConsentLevelPicker()
    } else if cell === disableNowCell {
      disableP2P()
    }
  }

  // MARK: - Actions

  private func showConsentLevelPicker() {
    let alertController = UIAlertController(
      title: L("p2p_consent_level_title"),
      message: L("p2p_consent_level_summary"),
      preferredStyle: .actionSheet
    )

    for level in [ConsentLevel.off, .viewOnly, .contribute] {
      let action = UIAlertAction(title: level.title, style: .default) { [weak self] _ in
        MWMP2PConsentManager.shared.setConsentLevel(MWMP2PConsentLevel(rawValue: level.rawValue)!)
        self?.updateUI()
      }
      alertController.addAction(action)
    }

    alertController.addAction(UIAlertAction(title: L("cancel"), style: .cancel))

    if let popover = alertController.popoverPresentationController {
      popover.sourceView = consentLevelCell
      popover.sourceRect = consentLevelCell.bounds
    }

    present(alertController, animated: true)
  }

  private func disableP2P() {
    MWMP2PConsentManager.shared.setConsentLevel(.off)
    MWMP2PTrafficService.shared.stop()
    updateUI()
  }
}

// MARK: - SettingsTableViewSwitchCellDelegate

extension P2PPrivacyViewController: SettingsTableViewSwitchCellDelegate {
  func switchCell(_ cell: SettingsTableViewSwitchCell, didChangeValue value: Bool) {
    if cell === debugModeCell {
      // TODO: Store debug mode preference
      // UserDefaults.standard.set(value, forKey: "P2PDebugMode")
    }
  }
}
