// Video of the view on macOS: the core draws each frame at the size and rate
// its camera plan says (dmc_camera_plan_of, dmc_video_frame); AVAssetWriter
// encodes them, as Android's ViewRecorder hands them to MediaCodec.
@preconcurrency import AVFoundation
import CDMCReader
import CoreVideo

@MainActor
final class ViewRecorder {
    private var writer: AVAssetWriter?
    private var input: AVAssetWriterInput?
    private var adaptor: AVAssetWriterInputPixelBufferAdaptor?
    private var timer: Timer?
    private var frameIndex: Int64 = 0
    private var fps: Int32 = 30
    private(set) var recording = false

    /// Starts recording the view to `url` (.mov). `frame` draws one frame into a buffer.
    func start(url: URL, viewWidth: Int, viewHeight: Int,
               frame: @escaping (_ rgba: UnsafeMutablePointer<UInt8>, _ capacity: Int) -> Bool) -> String? {
        let plan = dmc_camera_plan_of(true, Int32(viewWidth), Int32(viewHeight))
        guard plan.width > 0, plan.height > 0 else { return "The camera has no video plan for this view." }
        try? FileManager.default.removeItem(at: url)
        guard let writer = try? AVAssetWriter(outputURL: url, fileType: .mov) else { return "Could not create \(url.lastPathComponent)." }
        let codec: AVVideoCodecType = plan.format == 1 ? .hevc : .h264
        let settings: [String: Any] = [
            AVVideoCodecKey: codec,
            AVVideoWidthKey: Int(plan.width),
            AVVideoHeightKey: Int(plan.height),
            AVVideoCompressionPropertiesKey: [AVVideoAverageBitRateKey: max(1_000_000, Int(plan.quality))],
        ]
        let input = AVAssetWriterInput(mediaType: .video, outputSettings: settings)
        input.expectsMediaDataInRealTime = true
        let adaptor = AVAssetWriterInputPixelBufferAdaptor(
            assetWriterInput: input,
            sourcePixelBufferAttributes: [kCVPixelBufferPixelFormatTypeKey as String: kCVPixelFormatType_32BGRA,
                                          kCVPixelBufferWidthKey as String: Int(plan.width),
                                          kCVPixelBufferHeightKey as String: Int(plan.height)])
        guard writer.canAdd(input) else { return "This Mac cannot encode the video plan." }
        writer.add(input)
        guard writer.startWriting() else { return writer.error?.localizedDescription ?? "Could not start recording." }
        writer.startSession(atSourceTime: .zero)
        self.writer = writer
        self.input = input
        self.adaptor = adaptor
        fps = max(1, plan.fps)
        frameIndex = 0
        recording = true
        dmc_video_begin()
        let width = Int(plan.width), height = Int(plan.height)
        var rgba = [UInt8](repeating: 0, count: width * height * 4)
        timer = Timer.scheduledTimer(withTimeInterval: 1.0 / Double(fps), repeats: true) { [weak self] _ in
            Task { @MainActor in
                guard let self, self.recording else { return }
                let ok = rgba.withUnsafeMutableBufferPointer { frame($0.baseAddress!, $0.count) }
                if ok { self.append(rgba: rgba, width: width, height: height) }
            }
        }
        return nil
    }

    /// Stops and finishes the file; `done` gets the core-free outcome line.
    func stop(done: @escaping (String) -> Void) {
        timer?.invalidate()
        timer = nil
        recording = false
        guard let writer, let input else { done("Nothing was recorded."); return }
        input.markAsFinished()
        let frames = frameIndex
        let name = writer.outputURL.lastPathComponent
        Task { @MainActor in
            await writer.finishWriting()
            done(writer.status == .completed ? "Saved \(name) (\(frames) frames)."
                                              : (writer.error?.localizedDescription ?? "Recording failed."))
        }
        self.writer = nil
        self.input = nil
        self.adaptor = nil
    }

    private func append(rgba: [UInt8], width: Int, height: Int) {
        guard let input, let adaptor, input.isReadyForMoreMediaData, let pool = adaptor.pixelBufferPool else { return }
        var buffer: CVPixelBuffer?
        guard CVPixelBufferPoolCreatePixelBuffer(nil, pool, &buffer) == kCVReturnSuccess, let buffer else { return }
        CVPixelBufferLockBaseAddress(buffer, [])
        if let base = CVPixelBufferGetBaseAddress(buffer)?.assumingMemoryBound(to: UInt8.self) {
            let stride = CVPixelBufferGetBytesPerRow(buffer)
            rgba.withUnsafeBufferPointer { src in
                for y in 0..<height {
                    let row = base + y * stride
                    let from = y * width * 4
                    for x in 0..<width {  // RGBA -> BGRA
                        row[x * 4] = src[from + x * 4 + 2]
                        row[x * 4 + 1] = src[from + x * 4 + 1]
                        row[x * 4 + 2] = src[from + x * 4]
                        row[x * 4 + 3] = src[from + x * 4 + 3]
                    }
                }
            }
        }
        CVPixelBufferUnlockBaseAddress(buffer, [])
        if adaptor.append(buffer, withPresentationTime: CMTime(value: frameIndex, timescale: fps)) { frameIndex += 1 }
    }
}
