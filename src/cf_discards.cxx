
// cf_discards.cxx
// THIS FILE IS NOT INCLUDED IN THE BUILD

// It contains SCRAPS of discarded code, that may,
// or may not be useful soemtime in the future.

#if 0 // DISCARDED CODED

///////////////////////////////////////////////////////////////
// To generate 'test' strings to use in perl regex.pl
// --------------------------------------------------

// my $test1 = 'BR-NVS@LOCAL: 322514.895268 5645174.336093 2947179.140007 
// 27.687425 86.730184 9193.939175 
// -4.002387 -0.934358 0.737291 Aircraft/Embraer-195/Models/Embraer-195.xml';
// Test string to pass to perl - regex.pl
static int regex_count = 0;
char *get_regex_stg(PCF_Pilot pp)
{
    char *tb = GetNxtBuf();
    sprintf(tb,"%s@RELAY: %s %s %s %s",
        pp->callsign,
        get_point3d_stg2( &pp->SenderPosition ),
        get_point3d_stg2( &pp->GeodPoint ),
        get_point3d_stg2( &pp->SenderOrientation ),
        pp->aircraft );
    // ESPRTF("%s\n",tb);
    return tb;
}

#endif // 0 - DISCARDED CODE
// eof - cf_discards.cxx
