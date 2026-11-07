/* A plugin without the io buffer the engine needs (it does not use the entry point of the SDK): it must be refused with a
message that says what is missing. */

int vmMain( int command, int ioAddress, int unused1, int unused2 )
{
	return 0;
}
