import XCTest
import ObjectiveC

/// The game animates every frame, so XCTest's "wait for the app to idle" after each event
/// only ends by timeout (minutes). Replace the private quiescence wait with a no-op.
private func disableQuiescenceWait() {
    guard let cls = NSClassFromString("XCUIApplicationProcess") else { return }
    for name in ["waitForQuiescenceIncludingAnimationsIdle:", "waitForQuiescenceIncludingAnimationsIdle:isPreEvent:"] {
        guard let m = class_getInstanceMethod(cls, NSSelectorFromString(name)) else { continue }
        let noop: @convention(block) (AnyObject, Bool, Bool) -> Void = { _, _, _ in }
        method_setImplementation(m, imp_implementationWithBlock(noop))
    }
}

/// Drives the installed game with a step script passed through the environment
/// (xcodebuild forwards TEST_RUNNER_<NAME> as <NAME>):
///   STEPS   "rotate:left;wait:20;shot:title;tap:0.22,0.80;wait:5;shot:after"
///   OUT_DIR host directory for screenshots
///   LAUNCH  "1" to (re)launch the game first, otherwise attach to the running instance
final class DriveTests: XCTestCase {
    func testDrive() throws {
        disableQuiescenceWait()
        let env = ProcessInfo.processInfo.environment
        let steps = (env["STEPS"] ?? "").split(separator: ";").map(String.init)
        let outDir = URL(fileURLWithPath: env["OUT_DIR"] ?? NSTemporaryDirectory())
        let app = XCUIApplication(bundleIdentifier: env["APP_BUNDLE_ID"] ?? "com.example.manosaba")
        if env["LAUNCH"] == "1" { app.launch() } else { app.activate() }

        for step in steps {
            let parts = step.split(separator: ":", maxSplits: 1).map(String.init)
            let arg = parts.count > 1 ? parts[1] : ""
            NSLog("[drive] %@", step)
            switch parts[0] {
            case "rotate":
                XCUIDevice.shared.orientation = arg == "right" ? .landscapeRight : (arg == "portrait" ? .portrait : .landscapeLeft)
            case "wait":
                Thread.sleep(forTimeInterval: Double(arg) ?? 1)
            case "tap":
                let xy = arg.split(separator: ",").compactMap { Double($0) }
                app.coordinate(withNormalizedOffset: CGVector(dx: xy[0], dy: xy[1])).tap()
            case "shot":
                let shot = XCUIScreen.main.screenshot()
                let attachment = XCTAttachment(screenshot: shot)
                attachment.name = arg
                attachment.lifetime = .keepAlways
                add(attachment)
                // Host paths are only writable from the simulator; devices rely on the attachment.
                try? shot.pngRepresentation.write(to: outDir.appendingPathComponent("\(arg).png"))
            default:
                XCTFail("unknown step \(step)")
            }
        }
    }
}
