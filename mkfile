install:V:
	for(d in fxterm postfx/*) @{
		cd $d && mk install
	}

clean:V:
	for(d in fxterm postfx/*) @{
		cd $d && mk clean
	}
