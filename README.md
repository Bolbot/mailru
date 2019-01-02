# mailru
Final task for mail.ru course of multithreaded programming

Currently it's just bunch of sketches made in a hurry, please don't judge this code, it far from version I'd like to publish.
I was just curious if it actually works and does what task implied.

Generally it must be an HTTP server, implementing HTTP/1.0 protocol with approach of GET command and Status 200 and Status 404 response.
My additions to this was adding full support of HTTP/0.9 requests because it was required by HTTP/1.0 specification.
Also my version implements Status 400, Status 405 and Status 505 response for respective cases.

This version of server is using blocking IO with creating detached pthreads for each connection processing.

I just wonder how reliable is this approach on few thousands connections.
