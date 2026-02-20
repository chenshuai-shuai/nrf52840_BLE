# traini_grpc (echo server)

## Install
```bash
python -m pip install -r requirements.txt
```

## Generate Python stubs
```bash
python -m grpc_tools.protoc -I. --python_out=. --grpc_python_out=. traini.proto
```

## Run
```bash
python server.py
```

This starts an insecure (no TLS) echo gRPC server on `0.0.0.0:8080`.
