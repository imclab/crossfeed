/* 20130119 - 20130118 - show date and time 
 * from : http://www.webestools.com/scripts_tutorials-code-source-7-display-date-and-time-in-javascript-real-time-clock-javascript-date-time.html
 */

function date_time(id) {
         date = new Date;
         year = date.getFullYear();
         month = date.getMonth();
         months = new Array('January', 'February', 'March', 'April', 'May', 'June', 'Jully', 'August', 'September', 'October', 'November', 'December');
         d = date.getDate();
         day = date.getDay();
         days = new Array('Sunday', 'Monday', 'Tuesday', 'Wednesday', 'Thursday', 'Friday', 'Saturday');
         h = date.getHours();
         if(h<10) {
                 h = "0"+h;
         }
         m = date.getMinutes();
         if(m<10) {
                 m = "0"+m;
         }
         s = date.getSeconds();
         if(s<10) {
                 s = "0"+s;
         }
         result = ''+days[day]+' '+months[month]+' '+d+' '+year+' '+h+':'+m+':'+s;
         document.getElementById(id).innerHTML = result;
         setTimeout('date_time("'+id+'");','1000');
         return true;
}

function date_to_stg(date) {
    var year = date.getUTCFullYear();
    var mth = date.getUTCMonth()+1;
    var day = date.getUTCDate();
    var hr = date.getUTCHours();
    var min = date.getUTCMinutes();
    var sec = date.getUTCSeconds();
    var ms = date.getUTCMilliseconds();
    if (mth < 10) {
        mth = "0" + mth;
    }
    if (day < 10) {
        day = "0" + day;
    }
    if (hr < 10) {
        hr = "0" + hr;
    }
    if (min < 10) {
        min = "0" + min;
    }
    if (sec < 10) {
        sec = "0" + sec;
    }
    if (ms < 10) {
        ms = "00" + ms;
    } else {
        if (ms < 100) {
            ms = "0" + ms;
        }
    }
    var res = ""+year+"/"+mth+"/"+day+" "+hr+":"+min+":"+sec+"."+ms+" UTC";
    return res;
}


function get_utc_date_time_stg() {
    var date = new Date;
    return date_to_stg(date);
}

function date_time_utc(id) {
    var res = get_utc_date_time_stg();
    var obj = document.getElementById(id);
    obj.innerHTML = res;
    setTimeout('date_time_utc("'+id+'");','1000');
    return true;
}


function epoch1000_to_date_stg(ep) {
    var date = new Date(ep);
    return date_to_stg(date);
}
    
function date_to_short_stg(date) {
    //var year = date.getUTCFullYear();
    var mth = date.getUTCMonth()+1;
    var day = date.getUTCDate();
    var hr = date.getUTCHours();
    var min = date.getUTCMinutes();
    var sec = date.getUTCSeconds();
    var ms = date.getUTCMilliseconds();
    if (mth < 10) {
        mth = "0" + mth;
    }
    if (day < 10) {
        day = "0" + day;
    }
    if (hr < 10) {
        hr = "0" + hr;
    }
    if (min < 10) {
        min = "0" + min;
    }
    if (sec < 10) {
        sec = "0" + sec;
    }
    if (ms < 10) {
        ms = "00" + ms;
    } else {
        if (ms < 100) {
            ms = "0" + ms;
        }
    }
    var res = ""+mth+"/"+day+" "+hr+":"+min+":"+sec;
    return res;
}


function epoch1000_short_date_stg(ep) {
    var date = new Date(ep);
    return date_to_short_stg(date);
}

/* eof */
