-- 20120101 - Update to use VARCHAR(96) for the model

DROP TABLE IF EXISTS flights, positions CASCADE;



CREATE TABLE flights (
        f_pk SERIAL NOT NULL, 
        fid VARCHAR(24) NOT NULL, 
        callsign VARCHAR(16) NOT NULL, 
        model VARCHAR(20), 
        status VARCHAR(20), 
        PRIMARY KEY (f_pk)
);
CREATE INDEX ix_flights_model ON flights (model);
CREATE INDEX ix_flights_status ON flights (status);
CREATE UNIQUE INDEX ix_flights_fid ON flights (fid);
CREATE INDEX ix_flights_callsign ON flights (callsign);




CREATE TABLE positions (
        p_pk SERIAL NOT NULL, 
        fid VARCHAR(24) NOT NULL, 
        ts TIMESTAMP WITHOUT TIME ZONE NOT NULL, 
        lat VARCHAR(16) NOT NULL, 
        lon VARCHAR(16) NOT NULL, 
        spd_kts INTEGER, 
        alt_ft INTEGER, 
        hdg INTEGER, 
        PRIMARY KEY (p_pk)
);
CREATE INDEX ix_positions_fid ON positions (fid);
CREATE INDEX ix_positions_ts ON positions (ts);



create or replace view v_flights as
select
flights.fid, flights.callsign, flights.model, flights.status,
positions.lat, positions.lon, positions.spd_kts, 
positions.alt_ft, positions.hdg
from flights
inner join positions on flights.fid = positions.fid;

create or replace view v_flights_open as
select v_flights.*
from v_flights
where status = 'OPEN'
order by callsign;

-- eof
