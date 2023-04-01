
./server.out &

echo "server started"

sleep 1

./client.out set k 10

echo "client set k to 10"

sleep 1

echo "starting client spawn"

for i in {1..500000}; do ./client.out get k; done
