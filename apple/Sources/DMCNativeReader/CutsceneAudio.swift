// A cutscene's sound on macOS: the core mixes it (dmc_cutscene_audio_*), the
// shell only streams the samples to the speakers from the position the motion
// frame says, the same way Android's CutsceneAudio does.
import AVFoundation
import CDMCReader

final class CutsceneAudio: @unchecked Sendable {
    private let engine = AVAudioEngine()
    private var source: AVAudioSourceNode?
    private let lock = NSLock()
    private var session: ReaderSession?
    private var position: Int64 = 0
    private var channels = 2
    private var scratch = [Int16](repeating: 0, count: 8192 * 2)

    /// Starts the sound of `session` at motion frame `frame` (nothing when it has none).
    func play(_ session: ReaderSession, frame: Float) {
        stop()
        guard let format = session.cutsceneAudioFormat() else { return }
        lock.lock()
        self.session = session
        channels = format.channels
        position = session.cutsceneAudioPosition(frame: frame)
        lock.unlock()
        guard let avFormat = AVAudioFormat(commonFormat: .pcmFormatFloat32, sampleRate: Double(format.rate),
                                           channels: AVAudioChannelCount(format.channels), interleaved: false) else { return }
        let node = AVAudioSourceNode(format: avFormat) { [weak self] _, _, frameCount, bufferList -> OSStatus in
            self?.fill(frames: Int(frameCount), into: bufferList) ?? noErr
        }
        source = node
        engine.attach(node)
        engine.connect(node, to: engine.mainMixerNode, format: avFormat)
        do { try engine.start() } catch { stop() }
    }

    func stop() {
        engine.stop()
        if let source { engine.detach(source) }
        source = nil
        lock.lock()
        session = nil
        lock.unlock()
    }

    private func fill(frames: Int, into bufferList: UnsafeMutablePointer<AudioBufferList>) -> OSStatus {
        let buffers = UnsafeMutableAudioBufferListPointer(bufferList)
        lock.lock()
        defer { lock.unlock() }
        let wanted = min(frames, scratch.count / max(1, channels))
        var got = 0
        if let session {
            got = scratch.withUnsafeMutableBufferPointer { ptr in
                session.readCutsceneAudio(position: position, frames: wanted, into: ptr.baseAddress!, capacity: ptr.count)
            } / max(1, channels)
            position += Int64(got)
        }
        for (c, buffer) in buffers.enumerated() {
            guard let out = buffer.mData?.assumingMemoryBound(to: Float.self) else { continue }
            for i in 0..<frames {
                out[i] = i < got && c < channels ? Float(scratch[i * channels + c]) / 32768 : 0
            }
        }
        return noErr
    }
}
