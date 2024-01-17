Google Small Table is a small cloud platform including features such as webmail service and key-value storage service which is similar to Googleʼs Bigtable. We simulated a distributed systems environment with a load balancer for multiple instances of frontend servers as well as multiple backend servers with a master backend node. We also achieved fault tolerance and consistency with primary-based replication, having checkpoints and loggings in the disk.
                    
* Start with running the master node by:
```
cd backend
make ./master
```

* Then run one to three backend servers by:
```
cd backend ./server -v serverList.txt 1 ./server -v serverList.txt 2
```

* And run the frontend load balancer cd frontend:
```
make ./loadBalancer
```
Finally, run the frontend server: 
```
cd frontend
./sev -p 8080 ./sev -p 8081
```
Now you can open a browser and go to localhost:3000
