### Protocol draft

- TCP stateful connection

#### Auth + initialization
- First request = auth + set objectid to work with

#### IO requests
```
# read blocks
read <offset> <len>
```


#### examples:
```bash
$ telnet 127.0.0.1 8080
objid1 # set object id to work with in this connection
read 0 10 # read from 0 offset with 10 len
```
