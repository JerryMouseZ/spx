file spx_exchange
# set detach-on-fork off
set args products.txt ./spx_test_trader
b spx_exchange.c:200
r
