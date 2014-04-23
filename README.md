Comet server
============

Libevent based comet server simple pub/sub mechanism, used in an undergoing development.

### Commandline arguemnts

 - -v ```verbose logging mode name```
 - -a ```listening address, default 0.0.0.0```
 - -p ```listening port, default 80```
 - -i ```admin interfaces restiction by ip, default all*```

*** IP addresses must be separated with comma, ```all``` is a special value meaning no restrictions.

### Methods

Subscription

 - /sub 
 - channel=```channel name```
 - cb=```callback function name in jsonp```

Publishing

 - /pub
 - channel=```channel name```
 - message=```message```

Statistics

 - /stat
 
### Basic usage

Subscription to the channel:

    http://localhost:8080/sub?channel=chan-name
    
Publishing message to the subscribers:

    http://localhost:8080/pub?channel=chan-name&message=Hello+world