// One of a file's sounds (a VAG's chunks): the core decodes it to 16-bit mono
// PCM (dmc_sound_pcm); the shell only plays the samples, as Android does.
@preconcurrency import AVFoundation

@MainActor
final class SoundPlayer {
    private let engine = AVAudioEngine()
    private let player = AVAudioPlayerNode()
    private var format: AVAudioFormat?

    /// Plays `samples` at `rate` Hz from the start, stopping what played before.
    func play(samples: [Int16], rate: UInt32) {
        stop()
        guard !samples.isEmpty, rate > 0,
              let format = AVAudioFormat(commonFormat: .pcmFormatFloat32, sampleRate: Double(rate),
                                         channels: 1, interleaved: false),
              let buffer = AVAudioPCMBuffer(pcmFormat: format, frameCapacity: AVAudioFrameCount(samples.count)),
              let out = buffer.floatChannelData?[0] else { return }
        for (index, sample) in samples.enumerated() { out[index] = Float(sample) / 32768.0 }
        buffer.frameLength = AVAudioFrameCount(samples.count)
        if self.format != format {
            engine.disconnectNodeOutput(player)
            if player.engine == nil { engine.attach(player) }
            engine.connect(player, to: engine.mainMixerNode, format: format)
            self.format = format
        }
        do {
            if !engine.isRunning { try engine.start() }
        } catch {
            return
        }
        player.scheduleBuffer(buffer, at: nil, options: [])
        player.play()
    }

    func stop() {
        if player.engine != nil { player.stop() }
    }
}
