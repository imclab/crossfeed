-- *********************************************************
-- create_pg.sql from Pete, modified to work with SQLite3
-- 20121229 - Bump model to VARCHAR(96)
-- 20121213 - Re-work to again work for PostgreSQL
-- 20121210
-- psql -U cf tracker_test
-- # \i 'create_db.sql'
-- # \q
--
-- Cleanup previous, if any

-- remove indexed
DROP INDEX IF EXISTS ix_flights_model;
DROP INDEX IF EXISTS ix_flights_status;
DROP INDEX IF EXISTS ix_flights_fid;
DROP INDEX IF EXISTS ix_flights_callsign;
DROP INDEX IF EXISTS ix_positions_fid;
DROP INDEX IF EXISTS ix_positions_ts;

-- remove the READ ONLY views
-- DROP VIEW IF EXISTS v_fligths_open;
-- DROP VIEW IF EXISTS v_flights;

-- DROP TABLE IF EXISTS flights, positions CASCADE;
DROP TABLE IF EXISTS flights CASCADE;
DROP TABLE IF EXISTS positions CASCADE;

-- Create NEW

CREATE TABLE flights (
        f_pk SERIAL NOT NULL,
        fid VARCHAR(24) NOT NULL, 
        callsign VARCHAR(16) NOT NULL, 
        model VARCHAR(96), 
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

CREATE OR REPLACE VIEW v_flights as
select
flights.fid, flights.callsign, flights.model, flights.status,
positions.lat, positions.lon, positions.spd_kts, 
positions.alt_ft, positions.hdg
from flights
inner join positions on flights.fid = positions.fid;

CREATE OR REPLACE VIEW v_flights_open as
select v_flights.*
from v_flights
where status = 'OPEN'
order by callsign;

-- eof
