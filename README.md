## Exchange

### Start up

1. Parse correct command line arguments
2. Read the products file, and building a book names array for the products. 
3. Init traders data structure, which contains traders’ pid, pipe fd, and some values. 
4. Use `mkfifo` to create pipe files like  `/tmp/spx_exchange_<Trader ID>` or `/tmp/spx_trader_<Trader ID>`

5. Use `fork` and `exec` to create child process

6. Use `open` to connect to the pipe files. 
7. Notify all the traders with “MARKET OPEN;” using `write` and `kill`.

### Placing Orders

1. Read the command from the pipe file `spx_trader`. 
2. Check the order id, order type, product name, qty and price in the command. 
3. If the order is valid, record it in a sorted list, which will be used at the following sections. 
4. Response to the trader with message like `ACCEPTED`
5. Notify other traders the buy or sell information using `write` and `kill`. 

### Matching Orders

The orders are maintained in a sorted linked list, which are sorted from the highest-priced to the lowest-priced.  

1. Traverse the sorted orders’ list, find if some orders are matched to the new order.
2. Calculate the value and fee. 
3. Update the traders’ related values.
4. Remove order if it’s qty reach 0.
5. Notify the related trader with “FILL” message.

### Report

1. Traverse the orders’ list and print the orders. 
2. Traverse the traders and print the related values.

### Teardown

1. `SIGCHLD` handler marked the quit trader as invalid.
2. `SIGPIPE` handler marked the disconnect trader as invalid.

### Clean all

1. Free the traders data structure
2. Free the remaining orders
3. Free the productions’ book
4. Kill the disconnect but non-exited traders
5. Close all pipes

### IPC

At first, I have a try as reading message and handle the command in `SIGUSR1` handling, but found it can only receive one message of multiple message and signals. So I use a simple `FIFO` to store the `pid` of `SIGUSR1` and deal with it in the event loop. 

1. Wait for signals when the `FIFO` is empty using `pause` to release CPU resources. 
2. If the `FIFO` is not empty, consume the first `pid`, read the message from the related pipe, and handle the command which is mentioned as before. 



## Trader

1. Wait the market sending “OPEN” message. 
2. Loop at waiting for a “SELL” message, and then send “BUY” message. 
3. Waiting for “ACCEPTED” message

### Fault-tolerant

1. Check the the command from the message is valid
2. Keep reading from the pipe, instead of not reading until receive a `SIGUSR1` signal. 



## Test

I develop a test_trader for END-TO-END test, which read commands in a input file, and auto send commands to the exchange. 

For example

```
./spx_exchange products.txt ./spx_test_trader
```

The spx_test_trader will read commands which has been written in `input.txt` in the directory, and auto send it to the exchange, and we can get the result. 
