Start with running the master node by: cd backend make ./master

Then run one to three backend servers by: cd backend ./server -v serverList.txt 1 ./server -v serverList.txt 2

Then run the frontend load balancer cd frontend make ./loadBalancer

Finally run the frontend server: cd frontend ./sev -p 8080 ./sev -p 8081

A user can open the browser and go to localhost:3000
