#DeLorean-Mail Connect  
This is a simple program used to relay the offset given by SIP-Pi to others via TCP. It receives the offset via TCP from any SIP-Pi client (can only connect to one at the same time) using the Future Gadget Protocol. It distributes the offset to the postfix filter on request (which needs to run on the same machine as this program)  
It listens on Port 4242 for incoming TCP connections and port 1337 for communication via localhost via UDP.  
With ANY SIP-Pi client, i mean really every SIP-Pi client in this world can connect, first come first served :D  
No password authentification is implementend yet, use at your own risk!   
#####FGP – Future Gadget Protocol:  
REQTO : request current time offset  
RCVTO (int hours) : receive/set time offset (int hours)  
RCVOK : successfully received new offset  
MAILD : sent when mail processed  
ELPSY : used for connection keepalive  
KONGROO : used for connection keepalive  

#####How to install  

Clone this repository into your favourite location
then hit `make dmail-connect`
Then simply run dmail-connect. Put it into the autostart if needed with your favourite method.

#####Ideas not implemented yet:  
* Authentification through password for client
* encryption?
* listening to single IP address
* cleaning up the code, creating classes for socket and stuff
* Autostart scripts