remote=192.168.4.124

# Kill gdbserver if it's running
ssh root@${remote} killall gdbserver &> /dev/null

# Launch gdbserver, listening on port 9091
ssh root@${remote} "nohup gdbserver :9091 /home/root/pixy-i2c-test > /dev/null 2>&1 &"
