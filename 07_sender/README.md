# LwM2M Periodic Sender

`sender` queues periodic LwM2M Write requests through the DM server control socket.
The server owns the LwM2M and DTLS session; this process only schedules requests.

## Build

```sh
cmake -S . -B build
cmake --build build
```

## Run

Start the DM server with its control socket enabled:

```sh
../05_server/lwm2mserver -4 -p /tmp/lwm2mserver-control.sock
```

Queue the current Unix time every 60 seconds for client 0:

```sh
./build/sender -t 60 -c 0 -r /10250/0/1 -d now -i -v
```

Use `-n COUNT` for a finite test run and `-s PATH` to select a non-default socket.
`queued` means that the DM server accepted the local control request. The final
LwM2M Write response is logged by `lwm2mserver` asynchronously.