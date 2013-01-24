
// cf_discards.cxx
// THIS FILE IS NOT INCLUDED IN THE BUILD

// It contains SCRAPS of discarded code, that may,
// or may not be useful soemtime in the future.

#if 0 // DISCARDED CODED

#ifdef _OLD_INFO_JSON

static char _s_big_buf[1024];
const char *j_head = "{\"success\":true,\n\"source\":\"%s\",\n\"started\":\"%s\",\n\"info\":[{\n";
const char *info_json = 0;
std::string get_info_json() 
{
    std::string s;
    if (!info_json)
        return s;
    s = info_json;
    s += ",\n\t\"current_time\":\"";
    s += Get_Current_UTC_Time_Stg();
    s += " UTC\"";
    char *cp = _s_big_buf;
    s += ",\n\t\"secs_in_http\":\"";
    sprintf(cp,"%.1f",m_dSecs_in_HTTP);
    s += cp;
    s += "\"";
    if (m_dBegin_Secs > 0.0) {
        double t2 = get_seconds();
        s += ",\n\t\"secs_running\":\"";
        sprintf(cp,"%.1f",t2 - m_dBegin_Secs);
        s += cp;
        s += "\"";
    }
    // what else to ADD - maybe stats
#if defined(USE_POSTGRESQL_DATABASE) // TODO || defined(USE_SQLITE3_DATABASE))
    if (Enable_SQL_Tracker) {
        add_thread_stats_json(s);
    }
#endif
    s += "\n}]}\n";
    return s;
}

void set_init_json()
{
    char *cp = _s_big_buf;
    char *tp = Get_Current_UTC_Time_Stg();
    sprintf(cp, j_head, mod_name, tp);
    sprintf(EndBuf(cp),"\t\"TTL_secs\":\"%ld\",\n", m_PlayerExpires);
    sprintf(EndBuf(cp),"\t\"min_dist_m\":\"%d\",\n", (int)m_MinDistance_m);
    sprintf(EndBuf(cp),"\t\"min_speed_kt\":\"%d\",\n", m_MinSpdChange_kt);
    sprintf(EndBuf(cp),"\t\"min_hdg_chg_deg\":\"%d\",\n", m_MinHdgChange_deg);
    sprintf(EndBuf(cp),"\t\"min_alt_chg_ft\":\"%d\"\n", m_MinAltChange_ft);
    sprintf(EndBuf(cp),"\t\"tracker_log\":\"%s\"\n", 
        (tracker_log ? tracker_log : "none"));
#ifdef USE_POSTGRESQL_DATABASE
    if (Enable_SQL_Tracker)
        sprintf(EndBuf(cp),"\t\"tracker_db\":\"%s\"\n", get_pg_db_name());
    else
        sprintf(EndBuf(cp),"\t\"tracker_db\":\"DISABLED\"\n");
#else // #ifdef USE_POSTGRESQL_DATABASE
#ifdef USE_SQLITE3_DATABASE // ie !#ifdef USE_POSTGRESQL_DATABASE
    if (Enable_SQL_Tracker)
        sprintf(EndBuf(cp),"\t\"tracker_db\":\"%s\"\n", get_sqlite3_db_name());
    else
        sprintf(EndBuf(cp),"\t\"tracker_db\":\"DISABLED\"\n");
#endif // USE_SQLITE3_DATABASE // ie !#ifdef USE_POSTGRESQL_DATABASE
#endif // #ifdef USE_POSTGRESQL_DATABASE y/n
    // strcat(cp,"}]}\n");
    int len = (int)strlen(cp);
    cp[len-1] = ' ';    // convert last line end to space
    info_json = strdup(cp); // store this fixed header string
    SPRTF(get_info_json().c_str());
}

#else // #ifdef _OLD_INFO_JSON


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
