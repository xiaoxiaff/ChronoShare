function number_format( number, decimals, dec_point, thousands_sep ) {
    // http://kevin.vanzonneveld.net
    // +   original by: Jonas Raoni Soares Silva (http://www.jsfromhell.com)
    // +   improved by: Kevin van Zonneveld (http://kevin.vanzonneveld.net)
    // +     bugfix by: Michael White (http://crestidg.com)
    // +     bugfix by: Benjamin Lupton
    // +     bugfix by: Allan Jensen (http://www.winternet.no)
    // +    revised by: Jonas Raoni Soares Silva (http://www.jsfromhell.com)
    // *     example 1: number_format(1234.5678, 2, '.', '');
    // *     returns 1: 1234.57

    var n = number, c = isNaN(decimals = Math.abs(decimals)) ? 2 : decimals;
    var d = dec_point == undefined ? "," : dec_point;
    var t = thousands_sep == undefined ? "." : thousands_sep, s = n < 0 ? "-" : "";
    var i = parseInt(n = Math.abs(+n || 0).toFixed(c)) + "", j = (j = i.length) > 3 ? j % 3 : 0;

    return s + (j ? i.substr(0, j) + t : "") + i.substr(j).replace(/(\d{3})(?=\d)/g, "$1" + t) + (c ? d + Math.abs(n - i).toFixed(c).slice(2) : "");
}

function SegNumToFileSize (segNum) {
    filesize = segNum * 1024;

    if (filesize >= 1073741824) {
	filesize = number_format(filesize / 1073741824, 2, '.', '') + ' Gb';
    } else {
	if (filesize >= 1048576) {
     	    filesize = number_format(filesize / 1048576, 2, '.', '') + ' Mb';
   	} else {
	    if (filesize > 1024) {
    		filesize = number_format(filesize / 1024, 0) + ' Kb';
  	    } else {
    		filesize = '< 1 Kb';
	    };
 	};
    };
    return filesize;
};

/**
 * @brief Convert binary data represented as non-escaped hex string to Uint8Array
 * @param str String like ba0cb43e4b9639c114a0487d5faa7c70452533963fc8beb37d1b67c09a48a21d
 *
 * Note that if string length is odd, null will be returned
 */
StringHashToUint8Array = function (str) {
    if (str.length % 2 != 0) {
        return null;
    }

    var buf = new Uint8Array (str.length / 2);

    for (var i = 0; i < str.length; i+=2) {
        value = parseInt (str.substring (i, i+2), 16);
        buf[i/2] = value;
    }

    return buf;
};
