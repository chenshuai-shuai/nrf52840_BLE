import time
import wave
from pathlib import Path
from concurrent import futures
from datetime import datetime

import grpc

import traini_pb2
import traini_pb2_grpc


class ConversationService(traini_pb2_grpc.ConversationServiceServicer):
    def StreamConversation(self, request_iterator, context):
        out_dir = Path("recordings")
        out_dir.mkdir(parents=True, exist_ok=True)
        wav = None
        wav_path = None
        sample_rate = 16000
        channels = 1
        bit_depth = 16
        bytes_per_sample = 2

        for chunk in request_iterator:
            if wav is None:
                # Initialize WAV writer on first chunk.
                sample_rate = chunk.format.sample_rate or sample_rate
                channels = chunk.format.channels or channels
                bit_depth = chunk.format.bit_depth or bit_depth
                bytes_per_sample = max(1, bit_depth // 8)
                ts = datetime.utcnow().strftime("%Y%m%d_%H%M%S")
                wav_path = out_dir / f"rx_{ts}.wav"
                wav = wave.open(str(wav_path), "wb")
                wav.setnchannels(channels)
                wav.setsampwidth(bytes_per_sample)
                wav.setframerate(sample_rate)
                print(
                    f"WAV open: {wav_path} rate={sample_rate} ch={channels} depth={bit_depth}"
                )

            print(
                f"RX seq={chunk.sequence_number} bytes={len(chunk.audio_data)} "
                f"rate={chunk.format.sample_rate} ch={chunk.format.channels} depth={chunk.format.bit_depth}"
            )
            if wav is not None and chunk.audio_data:
                wav.writeframes(chunk.audio_data)

            # Echo PCM back as AudioOutput
            event = traini_pb2.ConversationEvent(
                audio_output=traini_pb2.AudioOutput(
                    audio_data=chunk.audio_data,
                    sequence_number=chunk.sequence_number,
                )
            )
            print(f"TX seq={chunk.sequence_number} bytes={len(chunk.audio_data)}")
            yield event

        if wav is not None:
            wav.close()
            print(f"WAV closed: {wav_path}")

    def EndConversation(self, request, context):
        return traini_pb2.SessionSummary(session_id=request.session_id)


def serve(host="0.0.0.0", port=8080):
    server = grpc.server(futures.ThreadPoolExecutor(max_workers=4))
    traini_pb2_grpc.add_ConversationServiceServicer_to_server(ConversationService(), server)
    server.add_insecure_port(f"{host}:{port}")
    server.start()
    print(f"gRPC echo server listening on {host}:{port}")
    try:
        while True:
            time.sleep(3600)
    except KeyboardInterrupt:
        server.stop(0)


if __name__ == "__main__":
    serve()
