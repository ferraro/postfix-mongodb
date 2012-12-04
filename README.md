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
consider it as alpha version.

If you need additional extra-features, please contact me at
contact@ferraro.net.

Example of a /etc/postfix/mmongodb-aliases.cf file:

	#
	# MongoDB Postfix Alias File
	# (C) Stephan Ferraro <stephan@ferraro.net>, Ferraro Ltd.
	#

	# Hostname
	host = 127.0.0.1

	# Port
	port = 27017

	# Use authentification to log into mongodb
	mongo_auth = 1
	mongo_user = root
	mongo_password = root

	# The database name on the servers.
	dbname = test

	# Search query by key and value
	# Example:
	# collection = address
	# key = name
	#
	# means in MongoDB search query: db.address.find({name: full_email_address});
	# The value with the full_email_address is filled up by Postfix
	collection = address
	key = name

The file can then be included so in the main.cf file:

	virtual_mailbox_maps = mongodb:/etc/postfix/mongodb-aliases.cf
