/* -------------------
   Geodesic calculations - 20120115
   Modifications made to deal with ONLY WGS-64 decimal latitude
   and longitude. rather than the original DMS format.
   Geoff R. McLane - reports _at_ geoffair _dot_ info
   
   Original from : http://movable-type.co.uk/scripts/latlong.html
   Copyright 2002-2012 Chris Veness
   Creative Commons License - http://creativecommons.org/licenses/by/3.0/
   I offer these formula & scripts for free use and adaptation 
   as my contribution to the open-source info-sphere from which 
   I have received so much. You are welcome to re-use these 
   scripts [under a simple attribution license, without any warranty 
   express or implied] provided solely that you retain my copyright 
   notice and a reference to this page.
   
   toRads = a * Math.PI/180;
   ------------------- */

var R = 6371; // km

/** Converts numeric degrees to radians */
if (typeof Number.prototype.toRad == 'undefined') {
  Number.prototype.toRad = function() {
    return this * Math.PI / 180;
  }
}

/** Converts radians to numeric (signed) degrees */
if (typeof Number.prototype.toDeg == 'undefined') {
  Number.prototype.toDeg = function() {
    return this * 180 / Math.PI;
  }
}

if (typeof Number.prototype.toPrecisionFixed == 'undefined') {
  Number.prototype.toPrecisionFixed = function(precision) {
    
    // use standard toPrecision method
    var n = this.toPrecision(precision);
    
    // ... but replace +ve exponential format with trailing zeros
    n = n.replace(/(.+)e\+(.+)/, function(n, sig, exp) {
      sig = sig.replace(/\./, '');       // remove decimal from significand
      l = sig.length - 1;
      while (exp-- > l) sig = sig + '0'; // append zeros from exponent
      return sig;
    });
    
    // ... and replace -ve exponential format with leading zeros
    n = n.replace(/(.+)e-(.+)/, function(n, sig, exp) {
      sig = sig.replace(/\./, '');       // remove decimal from significand
      while (exp-- > 1) sig = '0' + sig; // prepend zeros from exponent
      return '0.' + sig;
    });
    
    return n;
  }
}

/**
 * Creates a point on the earth's surface at the supplied latitude / longitude
 *
 * @constructor
 * @param {Number} lat: latitude in numeric degrees
 * @param {Number} lon: longitude in numeric degrees
 * @param {Number} [rad=6371]: radius of earth if different value is required from standard 6,371km
 */
function LatLon(lat, lon, rad) {
  if (typeof(rad) == 'undefined') rad = 6371;  // earth's mean radius in km
  // only accept numbers or valid numeric strings
  this._lat = typeof(lat)=='number' ? lat : typeof(lat)=='string' && lat.trim()!='' ? +lat : NaN;
  this._lon = typeof(lon)=='number' ? lon : typeof(lon)=='string' && lon.trim()!='' ? +lon : NaN;
  this._radius = typeof(rad)=='number' ? rad : typeof(rad)=='string' && trim(lon)!='' ? +rad : NaN;
}

LatLon.prototype.lat = function() {
  return this._lat;
}

LatLon.prototype.lon = function() {
  return this._lon;
}

/**
 * Returns the (initial) bearing from this point to the supplied point, in degrees
 *   see http://williams.best.vwh.net/avform.htm#Crs
 *
 * @param   {LatLon} point: Latitude/longitude of destination point
 * @returns {Number} Initial bearing in degrees from North
 */
LatLon.prototype.bearingTo = function(point) {
  var lat1 = this._lat.toRad(), lat2 = point._lat.toRad();
  var dLon = (point._lon-this._lon).toRad();

  var y = Math.sin(dLon) * Math.cos(lat2);
  var x = Math.cos(lat1)*Math.sin(lat2) -
          Math.sin(lat1)*Math.cos(lat2)*Math.cos(dLon);
  var brng = Math.atan2(y, x);
  
  return (brng.toDeg()+360) % 360;
}

/**
 * Returns final bearing arriving at supplied destination point from this point; the final bearing 
 * will differ from the initial bearing by varying degrees according to distance and latitude
 *
 * @param   {LatLon} point: Latitude/longitude of destination point
 * @returns {Number} Final bearing in degrees from North
 */
LatLon.prototype.finalBearingTo = function(point) {
  // get initial bearing from supplied point back to this point...
  var lat1 = point._lat.toRad(), lat2 = this._lat.toRad();
  var dLon = (this._lon-point._lon).toRad();

  var y = Math.sin(dLon) * Math.cos(lat2);
  var x = Math.cos(lat1)*Math.sin(lat2) -
          Math.sin(lat1)*Math.cos(lat2)*Math.cos(dLon);
  var brng = Math.atan2(y, x);
          
  // ... & reverse it by adding 180°
  return (brng.toDeg()+180) % 360;
}

/**
 * Returns the distance from this point to the supplied point, in km 
 * (using Haversine formula)
 *
 * from: Haversine formula - R. W. Sinnott, "Virtues of the Haversine",
 *       Sky and Telescope, vol 68, no 2, 1984
 *
 * @param   {LatLon} point: Latitude/longitude of destination point
 * @param   {Number} [precision=4]: no of significant digits to use for returned value
 * @returns {Number} Distance in km between this point and destination point
 */
LatLon.prototype.distanceTo = function(point, precision) {
  // default 4 sig figs reflects typical 0.3% accuracy of spherical model
  if (typeof precision == 'undefined') precision = 4;
  
  var R = this._radius;
  var lat1 = this._lat.toRad(), lon1 = this._lon.toRad();
  var lat2 = point._lat.toRad(), lon2 = point._lon.toRad();
  var dLat = lat2 - lat1;
  var dLon = lon2 - lon1;

  var a = Math.sin(dLat/2) * Math.sin(dLat/2) +
          Math.cos(lat1) * Math.cos(lat2) * 
          Math.sin(dLon/2) * Math.sin(dLon/2);
  var c = 2 * Math.atan2(Math.sqrt(a), Math.sqrt(1-a));
  var d = R * c;
  return d.toPrecisionFixed(precision);
}

/**
 * Returns the midpoint between this point and the supplied point.
 *   see http://mathforum.org/library/drmath/view/51822.html for derivation
 *
 * @param   {LatLon} point: Latitude/longitude of destination point
 * @returns {LatLon} Midpoint between this point and the supplied point
 */
LatLon.prototype.midpointTo = function(point) {
  lat1 = this._lat.toRad(), lon1 = this._lon.toRad();
  lat2 = point._lat.toRad();
  var dLon = (point._lon-this._lon).toRad();

  var Bx = Math.cos(lat2) * Math.cos(dLon);
  var By = Math.cos(lat2) * Math.sin(dLon);

  lat3 = Math.atan2(Math.sin(lat1)+Math.sin(lat2),
                    Math.sqrt( (Math.cos(lat1)+Bx)*(Math.cos(lat1)+Bx) + By*By) );
  lon3 = lon1 + Math.atan2(By, Math.cos(lat1) + Bx);
  lon3 = (lon3+3*Math.PI) % (2*Math.PI) - Math.PI;  // normalise to -180..+180º
  
  return new LatLon(lat3.toDeg(), lon3.toDeg());
}

/**
 * Returns the destination point from this point having travelled the given distance (in km) on the 
 * given initial bearing (bearing may vary before destination is reached)
 *
 *   see http://williams.best.vwh.net/avform.htm#LL
 *
 * @param   {Number} brng: Initial bearing in degrees
 * @param   {Number} dist: Distance in km
 * @returns {LatLon} Destination point
 */
LatLon.prototype.destinationPoint = function(brng, dist) {
  dist = typeof(dist)=='number' ? dist : typeof(dist)=='string' && dist.trim()!='' ? +dist : NaN;
  dist = dist/this._radius;  // convert dist to angular distance in radians
  brng = brng.toRad();  // 
  var lat1 = this._lat.toRad(), lon1 = this._lon.toRad();

  var lat2 = Math.asin( Math.sin(lat1)*Math.cos(dist) + 
                        Math.cos(lat1)*Math.sin(dist)*Math.cos(brng) );
  var lon2 = lon1 + Math.atan2(Math.sin(brng)*Math.sin(dist)*Math.cos(lat1), 
                               Math.cos(dist)-Math.sin(lat1)*Math.sin(lat2));
  lon2 = (lon2+3*Math.PI) % (2*Math.PI) - Math.PI;  // normalise to -180..+180º

  return new LatLon(lat2.toDeg(), lon2.toDeg());
}

function getDistance( lat1, lon1, lat2, lon2 ) {
    var p1 = new LatLon(lat1,lon1);
    var p2 = new LatLon(lat2,lon2);
    return p1.distanceTo(p2);
}

function getBearing( lat1, lon1, lat2, lon2 ) {
   var dLon = (lon2-lon1).toRad();
   var lat1 = lat1.toRad();
   var lat2 = lat2.toRad();

   var y = Math.sin(dLon) * Math.cos(lat2);
   var x = Math.cos(lat1)*Math.sin(lat2) -
        Math.sin(lat1)*Math.cos(lat2)*Math.cos(dLon);
   var brng = Math.atan2(y, x).toDeg();
   return (brng.toDeg()+360) % 360;
}


function getDirect( lat1, lon1, brng, dist ) {
    var p1 = new LatLon(lat1, lon1);
    var p2 = p1.destinationPoint(brng, dist);
    return p2;
}

/* - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -  */
/* rhumb line functions */

/**
 * Returns the distance from this point to the supplied point, in km, travelling along a rhumb line
 *
 *   see http://williams.best.vwh.net/avform.htm#Rhumb
 *
 * @param   {LatLon} point: Latitude/longitude of destination point
 * @returns {Number} Distance in km between this point and destination point
 */
LatLon.prototype.rhumbDistanceTo = function(point) {
  var R = this._radius;
  var lat1 = this._lat.toRad(), lat2 = point._lat.toRad();
  var dLat = (point._lat-this._lat).toRad();
  var dLon = Math.abs(point._lon-this._lon).toRad();
  
  var dPhi = Math.log(Math.tan(lat2/2+Math.PI/4)/Math.tan(lat1/2+Math.PI/4));
  var q = (isFinite(dLat/dPhi)) ? dLat/dPhi : Math.cos(lat1);  // E-W line gives dPhi=0
  
  // if dLon over 180° take shorter rhumb across anti-meridian:
  if (Math.abs(dLon) > Math.PI) {
    dLon = dLon>0 ? -(2*Math.PI-dLon) : (2*Math.PI+dLon);
  }
  
  var dist = Math.sqrt(dLat*dLat + q*q*dLon*dLon) * R; 
  
  return dist.toPrecisionFixed(4);  // 4 sig figs reflects typical 0.3% accuracy of spherical model
}

/**
 * Returns the bearing from this point to the supplied point along a rhumb line, in degrees
 *
 * @param   {LatLon} point: Latitude/longitude of destination point
 * @returns {Number} Bearing in degrees from North
 */
LatLon.prototype.rhumbBearingTo = function(point) {
  var lat1 = this._lat.toRad(), lat2 = point._lat.toRad();
  var dLon = (point._lon-this._lon).toRad();
  
  var dPhi = Math.log(Math.tan(lat2/2+Math.PI/4)/Math.tan(lat1/2+Math.PI/4));
  if (Math.abs(dLon) > Math.PI) dLon = dLon>0 ? -(2*Math.PI-dLon) : (2*Math.PI+dLon);
  var brng = Math.atan2(dLon, dPhi);
  
  return (brng.toDeg()+360) % 360;
}

/**
 * Returns the destination point from this point having travelled the given distance (in km) on the 
 * given bearing along a rhumb line
 *
 * @param   {Number} brng: Bearing in degrees from North
 * @param   {Number} dist: Distance in km
 * @returns {LatLon} Destination point
 */
LatLon.prototype.rhumbDestinationPoint = function(brng, dist) {
  var R = this._radius;
  var d = parseFloat(dist)/R;  // d = angular distance covered on earth’s surface
  var lat1 = this._lat.toRad(), lon1 = this._lon.toRad();
  brng = brng.toRad();

  var dLat = d*Math.cos(brng);
  // nasty kludge to overcome ill-conditioned results around parallels of latitude:
  if (Math.abs(dLat) < 1e-10) dLat = 0; // dLat < 1 mm
  
  var lat2 = lat1 + dLat;
  var dPhi = Math.log(Math.tan(lat2/2+Math.PI/4)/Math.tan(lat1/2+Math.PI/4));
  var q = (isFinite(dLat/dPhi)) ? dLat/dPhi : Math.cos(lat1);  // E-W line gives dPhi=0
  var dLon = d*Math.sin(brng)/q;
  
  // check for some daft bugger going past the pole, normalise latitude if so
  if (Math.abs(lat2) > Math.PI/2) lat2 = lat2>0 ? Math.PI-lat2 : -Math.PI-lat2;
  
  lon2 = (lon1+dLon+3*Math.PI)%(2*Math.PI) - Math.PI;
 
  return new LatLon(lat2.toDeg(), lon2.toDeg());
}

/**
 * Returns the loxodromic midpoint (along a rhumb line) between this point and the supplied point.
 *   see http://mathforum.org/kb/message.jspa?messageID=148837
 *
 * @param   {LatLon} point: Latitude/longitude of destination point
 * @returns {LatLon} Midpoint between this point and the supplied point
 */
LatLon.prototype.rhumbMidpointTo = function(point) {
  lat1 = this._lat.toRad(), lon1 = this._lon.toRad();
  lat2 = point._lat.toRad(), lon2 = point._lon.toRad();
  
  if (Math.abs(lon2-lon1) > Math.PI) lon1 += 2*Math.PI; // crossing anti-meridian
  
  var lat3 = (lat1+lat2)/2;
  var f1 = Math.tan(Math.PI/4 + lat1/2);
  var f2 = Math.tan(Math.PI/4 + lat2/2);
  var f3 = Math.tan(Math.PI/4 + lat3/2);
  var lon3 = ( (lon2-lon1)*Math.log(f3) + lon1*Math.log(f2) - lon2*Math.log(f1) ) / Math.log(f2/f1);
  
  if (!isFinite(lon3)) lon3 = (lon1+lon2)/2; // parallel of latitude
  
  lon3 = (lon3+3*Math.PI) % (2*Math.PI) - Math.PI;  // normalise to -180..+180º
  
  return new LatLon(lat3.toDeg(), lon3.toDeg());
}

/* - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -  */

/* - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -  */

/*
 * extend String object with trim method (if not built in)
 *   note efficient formulation from blog.stevenlevithan.com
 */
if (typeof(String.prototype.trim) === "undefined") {
  String.prototype.trim = function() {
    return String(this).replace(/^\s\s*/, '').replace(/\s\s*$/, '');
  }
}

/*
 * add isNumber function (if not built in)
 *   JavaScript: The Good Parts, Douglas Crockford, O'Reilly
 */
if (typeof(isNumber) === "undefined") {
  var isNumber = function isNumber(value) { 
    return typeof value === 'number' && isFinite(value); 
  };
}

/* - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -  */

/* eof */
