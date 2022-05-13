file spx_exchange
# set detach-on-fork off
# set follow-fork-mode child
set args products.txt ./spx_trader ./spx_test_trader1
b main
# b pipe_handler
# b child_handler
r
