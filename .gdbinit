file spx_exchange
# set detach-on-fork off
set follow-fork-mode child
set args products.txt ./spx_test_trader
b main
# b pipe_handler
# b child_handler
r
