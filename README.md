Postfix for MongoDB
===================

Postfix for MongoDB is a fork of the famous Postfix SMTP daemon.
It allows to store virtual email addresses in a MongoDB database.

So the difference between the official and this version is that
the MongoDB dict lookup driver has been included.
As long the C Driver of MongoDB (libmongoc) is not yet realized as
official packages for main-stream Linux and BSD distributions,
this dictionnary driver can not be included in the official version
of Postfix daemon.

This is the first version, with very basic functions, so please
consider it as alpha version and only be used by advanced and
experienced users of Postfix with C programming knowledges.

In future, it would be good that this driver would be included in
the official postfix release.

The postfix version used here is 3.4.6.

If you need additional extra-features, please contact me at
contact@aionda.com.

Example of a /etc/postfix/mmongodb-aliases.cf file:

	#
	# MongoDB Postfix Alias File
	# (C) Stephan Ferraro <stephan@ferraro.net>, Ferraro Ltd.
	#

	# URI
	uri = mongodb://root:root@localhost:27017/?appname=postfix

	# The database name on the servers.
	dbname = test

	
	# Search query by key and return value
	# Example:
	# collection = address
	# key = email
	#
	# This means in MongoDB JavaScript search query:
	# use address;
	# var response = db.address.find({email: full_email_address}); 
	# console.log(response.email);
	#
	# It would then return the property newEmail of the found object returned by MongoDB find() command.   
	# The value with the full_email_address is filled up by Postfix.
	collection = address
	key = email

The file can then be included so in the main.cf file:

	virtual_mailbox_maps = mongodb:/etc/postfix/mongodb-aliases.cf

Features
========
- find and return string value of the found object
- supports Plus Addressing formats, e.g. address+test@domain.tld
- reconnect to MongoDB server if it was gone or restarted

Installation
============

1. Install Ubuntu Linux Server 19.04 (maybe it works too on other Linux distributions, but I have not tested it)

2. Install C compiler and development environment

        apt-get install make m4 gcc libc-dev libdb-dev libssl-dev libglib2.0-dev cmake libssl-dev libsasl2-dev

3. Install MongoDB libraries at Ubuntu 19.04:

        apt-get install libbson-1.0 libbson-dev libmongoc-1.0-0 libmongoc-dev

4. Compile this postfix source code, on Ubuntu Linux you need to run:

		make tidy
		make makefiles CCARGS="-DUSE_TLS" AUXLIBS="-lssl -lcrypto"
		make SYSLIBS="-L/usr/lib/x86_64-linux-gnu -lssl -lcrypto -lpcre -ldb -lnsl -lresolv -lbson-1.0 -lmongoc-1.0"
		make install

4. Start MongoDB server
5. Configure Postfix configuration files, specially add in main.cf the entry "virtual_mailbox_maps = mongodb:/etc/postfix/mongodb-aliases.cf" and add the file /etc/postfix/mongodb-aliases.cf
6. Start Postfix server with the command "/usr/sbin/postfix start" and check log file at /var/log/mail.log for debugging purposes

Testing
=======
Simply try if it works by executing the following postfix command:

       # postmap -q 'address@domain.tld' mongodb:/etc/postfix/mongodb-aliases.cf 
       address@domain.tld
       # postmap -q 'address+test@domain.tld' mongodb:/etc/postfix/mongodb-aliases.cf 
       address@domain.tld

If nothing appears, then postmap command should return with an exit code of 1. You can check it with the echo command:

      # echo $?
      1

Difference between original Postfix version
===========================================
List of additional src files compared to Postfix normal version:
- global/dict_mongodb.h
- global/dict_mongodb.c

List of modified src files compared to Postfix normal version:
- global/Makefile.in
- global/mail_dict.c
- master/master.c

Implementing new features
=========================
If you need new features, please contact me at contact@aionda.com.
Any project work need to be paid. Additionally any work on it it will be included for everyone in this GitHub repository as its an open source project.
