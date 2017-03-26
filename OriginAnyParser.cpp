/*
 * OriginAnyParser.cpp
 *
 * Copyright 2017 Miquel Garriga <gbmiquel@gmail.com>
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 * Parser for all versions. Based mainly on Origin750Parser.cpp
 */

#include "OriginAnyParser.h"
#include <sstream>

/* define a macro to get an int (or uint) from a istringstream in binary mode */
#define GET_INT(iss, ovalue) {iss.read(reinterpret_cast<char *>(&ovalue), 4);};
#define GET_SHORT(iss, ovalue) {iss.read(reinterpret_cast<char *>(&ovalue), 2);};
#define GET_FLOAT(iss, ovalue) {iss.read(reinterpret_cast<char *>(&ovalue), 4);};
#define GET_DOUBLE(iss, ovalue) {iss.read(reinterpret_cast<char *>(&ovalue), 8);};

OriginAnyParser::OriginAnyParser(const string& fileName)
:	file(fileName.c_str(),ios::binary)
{
	objectIndex = 0;
}

bool OriginAnyParser::parse() {
#ifndef NO_CODE_GENERATION_FOR_LOG
	// append progress in log file
	logfile = fopen("opjfile.log","a");
#endif // NO_CODE_GENERATION_FOR_LOG

	// get length of file:
	file.seekg (0, ios_base::end);
	d_file_size = file.tellg();
	file.seekg(0, ios_base::beg);

	LOG_PRINT(logfile, "File size: %d\n", d_file_size)

	// get file and program version, check it is a valid file
	readFileVersion();
	unsigned long curpos = 0;
	curpos = file.tellg();
	LOG_PRINT(logfile, "Now at %d [0x%X]\n", curpos, curpos)

	// get global header
	readGlobalHeader();
	curpos = file.tellg();
	LOG_PRINT(logfile, "Now at %d [0x%X]\n", curpos, curpos)

	// get dataset list
	unsigned int dataset_list_size = 0;

	LOG_PRINT(logfile, "Reading Data sets ...\n")
	while (true) {
		if (!readDataSetElement()) break;
		dataset_list_size++;
	}
	LOG_PRINT(logfile, " ... done. Data sets: %d\n", dataset_list_size)
	curpos = file.tellg();
	LOG_PRINT(logfile, "Now at %d [0x%X], filesize %d\n", curpos, curpos, d_file_size)

	for(unsigned int i = 0; i < speadSheets.size(); ++i){
		if(speadSheets[i].sheets > 1){
			LOG_PRINT(logfile, "		CONVERT SPREADSHEET \"%s\" to EXCEL\n", speadSheets[i].name.c_str());
			convertSpreadToExcel(i);
			--i;
		}
	}

	// get window list
	unsigned int window_list_size = 0;

	LOG_PRINT(logfile, "Reading Windows ...\n")
	while (true) {
		if (!readWindowElement()) break;
		window_list_size++;
	}
	LOG_PRINT(logfile, " ... done. Windows: %d\n", window_list_size)
	curpos = file.tellg();
	LOG_PRINT(logfile, "Now at %d [0x%X], filesize %d\n", curpos, curpos, d_file_size)

	// get parameter list
	unsigned int parameter_list_size = 0;

	LOG_PRINT(logfile, "Reading Parameters ...\n")
	while (true) {
		if (!readParameterElement()) break;
		parameter_list_size++;
	}
	LOG_PRINT(logfile, " ... done. Parameters: %d\n", parameter_list_size)
	curpos = file.tellg();
	LOG_PRINT(logfile, "Now at %d [0x%X], filesize %d\n", curpos, curpos, d_file_size)

	// Note windows were added between version >4.141 and 4.210,
	// i.e., with Release 5.0
	if (curpos >= d_file_size) {
		LOG_PRINT(logfile, "Now at end of file\n")
		return true;
	}

	// get note windows list
	unsigned int note_list_size = 0;

	LOG_PRINT(logfile, "Reading Note windows ...\n")
	while (true) {
		if (!readNoteElement()) break;
		note_list_size++;
	}
	LOG_PRINT(logfile, " ... done. Note windows: %d\n", note_list_size)
	curpos = file.tellg();
	LOG_PRINT(logfile, "Now at %d [0x%X], filesize %d\n", curpos, curpos, d_file_size)

	// Project Tree was added between version >4.210 and 4.2616,
	// i.e., with Release 6.0
	if (curpos >= d_file_size) {
		LOG_PRINT(logfile, "Now at end of file\n")
		return true;
	}

	// get project tree
	readProjectTree();
	curpos = file.tellg();
	LOG_PRINT(logfile, "Now at %d [0x%X], filesize %d\n", curpos, curpos, d_file_size)

	// Attachments were added between version >4.2673_558 and 4.2764_623,
	// i.e., with Release 7.0
	if (curpos >= d_file_size) {
		LOG_PRINT(logfile, "Now at end of file\n")
		return true;
	}

	readAttachmentList();
	curpos = file.tellg();
	LOG_PRINT(logfile, "Now at %d [0x%X], filesize %d\n", curpos, curpos, d_file_size)
	if (curpos >= d_file_size) LOG_PRINT(logfile, "Now at end of file\n")

	return true;
}

string toLowerCase(string str){
	for (unsigned int i = 0; i < str.length(); i++)
		if (str[i] >= 0x41 && str[i] <= 0x5A)
			str[i] = str[i] + 0x20;
	return str;
}

OriginParser* createOriginAnyParser(const string& fileName)
{
	return new OriginAnyParser(fileName);
}

unsigned int OriginAnyParser::readObjectSize() {
	unsigned int obj_size = 0;
	char c = 0;
	file >> obj_size;
	file >> c;
	if (c != '\n') {
		LOG_PRINT(logfile, "Wrong delimiter %c at %d [0x%X]\n", c, (unsigned long)file.tellg(), (unsigned long)file.tellg())
		exit(2);
	}
	return obj_size;
}

string OriginAnyParser::readObjectAsString(unsigned int size) {
	char c;
	// read a size-byte blob of data followed by '\n'
	if (size > 0) {
		// get a string large enough to hold the result, initialize it to all 0's
		string blob = string(size, '\0');
		// read data into that string
		// cannot use '>>' operator because iendianfstream truncates it at first '\0'
		file.read(&blob[0], size);
		// read the '\n'
		file >> c;
		if (c != '\n') {
			LOG_PRINT(logfile, "Wrong delimiter %c at %d [0x%X]\n", c, (unsigned long)file.tellg(), (unsigned long)file.tellg())
			exit(3);
		}
		return blob;
	}
	return string();
}

void OriginAnyParser::readFileVersion() {
	// get file and program version, check it is a valid file
	string sFileVersion;
	getline(file, sFileVersion);

	if ((sFileVersion.substr(0,4) != "CPYA") or (*sFileVersion.rbegin() != '#')) {
		LOG_PRINT(logfile, "File, is not a valid opj file\n")
		exit(1);
	}
	LOG_PRINT(logfile, "File version string: %s\n", sFileVersion.c_str())
}

void OriginAnyParser::readGlobalHeader() {
	// get global header size
	unsigned int gh_size = 0, gh_endmark = 0;
	gh_size = readObjectSize();
	unsigned long curpos = file.tellg();
	LOG_PRINT(logfile, "Global header size: %d [0x%X], starts at %d [0x%X],", gh_size, gh_size, curpos, curpos)

	// get global header data
	string gh_data;
	gh_data = readObjectAsString(gh_size);

	curpos = file.tellg();
	LOG_PRINT(logfile, " ends at %d [0x%X]\n", curpos, curpos)

	// when gh_size > 0x1B, a double with fileVersion/100 can be read at gh_data[0x1B:0x23]
	if (gh_size > 0x1B) {
		istringstream stmp;
		stmp.str(gh_data.substr(0x1B));
		double dFileVersion;
		GET_DOUBLE(stmp, dFileVersion)
		if (dFileVersion > 8.5) {
			fileVersion = (unsigned int)trunc(dFileVersion*100.);
		} else {
			fileVersion = 10*(unsigned int)trunc(dFileVersion*10.);
		}
		LOG_PRINT(logfile, "Project version as read from header: %.2f (%.6f)\n", fileVersion/100.0, dFileVersion)
	}

	// now read a zero size end mark
	gh_endmark = readObjectSize();
	if (gh_endmark != 0) {
		LOG_PRINT(logfile, "Wrong end of list mark %d at %d [0x%X]\n", gh_endmark, (unsigned long)file.tellg(), (unsigned long)file.tellg())
		exit(4);
	}
}

bool OriginAnyParser::readDataSetElement() {
	/* get info and values of a DataSet (worksheet column, matrix sheet, ...)
	 * return true if a DataSet is found, otherwise return false */
	unsigned int dse_header_size = 0, dse_data_size = 0, dse_mask_size = 0;
	unsigned long curpos = 0, dsh_start = 0, dsd_start = 0, dsm_start = 0;
	string dse_header;

	// get dataset header size
	dse_header_size = readObjectSize();
	if (dse_header_size == 0) return false;

	curpos = file.tellg();
	dsh_start = curpos;
	LOG_PRINT(logfile, "Column: header size %d [0x%X], starts at %d [0x%X], ", dse_header_size, dse_header_size, curpos, curpos)
	dse_header = readObjectAsString(dse_header_size);

	// get known info
	string name(25,0);
	name = dse_header.substr(0x58,25);

	// go to end of dataset header, get data size
	file.seekg(dsh_start+dse_header_size+1, ios_base::beg);
	dse_data_size = readObjectSize();
	dsd_start = file.tellg();
	string dse_data = readObjectAsString(dse_data_size);
	curpos = file.tellg();
	LOG_PRINT(logfile, "data size %d [0x%X], from %d [0x%X] to %d [0x%X],", dse_data_size, dse_data_size, dsd_start, dsd_start, curpos, curpos)

	// get data values
	getColumnInfoAndData(dse_header, dse_header_size, dse_data, dse_data_size);

	// go to end of data values, get mask size (often zero)
	file.seekg(dsd_start+dse_data_size, ios_base::beg); // dse_data_size can be zero
	if (dse_data_size > 0) file.seekg(1, ios_base::cur);
	dse_mask_size = readObjectSize();
	dsm_start = file.tellg();
	if (dse_mask_size > 0) LOG_PRINT(logfile, "\nmask size %d [0x%X], starts at %d [0x%X]", dse_mask_size, dse_mask_size, dsm_start, dsm_start)
	string dse_mask = readObjectAsString(dse_mask_size);

	// get mask values
	if (dse_mask_size > 0) {
		curpos = file.tellg();
		LOG_PRINT(logfile, ", ends at %d [0x%X]\n", curpos, curpos)
		// TODO: extract mask values from dse_mask
		// go to end of dataset mask
		file.seekg(dsm_start+dse_mask_size+1, ios_base::beg);
	}
	curpos = file.tellg();
	LOG_PRINT(logfile, " ends at %d [0x%X]: ", curpos, curpos)
	LOG_PRINT(logfile, "%s\n", name.c_str())

	return true;
}

bool OriginAnyParser::readWindowElement() {
	/* get general info and details of a window
	 * return true if a Window is found, otherwise return false */
	unsigned int wde_header_size = 0;
	unsigned long curpos = 0, wdh_start = 0;

	// get window header size
	wde_header_size = readObjectSize();
	if (wde_header_size == 0) return false;

	curpos = file.tellg();
	wdh_start = curpos;
	LOG_PRINT(logfile, "Window found: header size %d [0x%X], starts at %d [0x%X]: ", wde_header_size, wde_header_size, curpos, curpos)
	string wde_header = readObjectAsString(wde_header_size);

	// get known info
	string name(25,0);
	name = wde_header.substr(0x02,25).c_str();
	LOG_PRINT(logfile, "%s\n", name.c_str())

	// classify type of window
	ispread = findSpreadByName(name);
	imatrix = findMatrixByName(name);
	iexcel  = findExcelByName(name);
	igraph = -1;

	if (ispread != -1) {
		LOG_PRINT(logfile, "\n  Window is a Worksheet book\n")
		getWindowProperties(speadSheets[ispread], wde_header, wde_header_size);
	} else if (imatrix != -1) {
		LOG_PRINT(logfile, "\n  Window is a Matrix book\n")
		getWindowProperties(matrixes[imatrix], wde_header, wde_header_size);
	} else if (iexcel != -1) {
		LOG_PRINT(logfile, "\n  Window is an Excel book\n")
		getWindowProperties(excels[iexcel], wde_header, wde_header_size);
	} else {
		LOG_PRINT(logfile, "\n  Window is a Graph\n")
		graphs.push_back(Graph(name));
		igraph = graphs.size()-1;
		getWindowProperties(graphs[igraph], wde_header, wde_header_size);
	}

	// go to end of window header
	file.seekg(wdh_start+wde_header_size+1, ios_base::beg);

	// get layer list
	unsigned int layer_list_size = 0;

	LOG_PRINT(logfile, " Reading Layers ...\n")
	while (true) {
		ilayer = layer_list_size;
		if (!readLayerElement()) break;
		layer_list_size++;
	}
	LOG_PRINT(logfile, " ... done. Layers: %d\n", layer_list_size)
	curpos = file.tellg();
	LOG_PRINT(logfile, "window ends at %d [0x%X]\n", curpos, curpos)

	return true;
}

bool OriginAnyParser::readLayerElement() {
	/* get general info and details of a layer
	 * return true if a Layer is found, otherwise return false */
	unsigned int lye_header_size = 0;
	unsigned long curpos = 0, lyh_start = 0;

	// get layer header size
	lye_header_size = readObjectSize();
	if (lye_header_size == 0) return false;

	curpos = file.tellg();
	lyh_start = curpos;
	LOG_PRINT(logfile, "  Layer found: header size %d [0x%X], starts at %d [0x%X]\n", lye_header_size, lye_header_size, curpos, curpos)
	string lye_header = readObjectAsString(lye_header_size);

	// get known info
	getLayerProperties(lye_header, lye_header_size);

	// go to end of layer header
	file.seekg(lyh_start+lye_header_size+1, ios_base::beg);

	// get annotation list
	unsigned int annotation_list_size = 0;

	LOG_PRINT(logfile, "   Reading Annotations ...\n")
	/* Some annotations can be groups of annotations. We need a recursive function for those cases */
	annotation_list_size = readAnnotationList();
	LOG_PRINT(logfile, "   ... done. Annotations: %d\n", annotation_list_size)

	// get curve list
	unsigned int curve_list_size = 0;

	LOG_PRINT(logfile, "   Reading Curves ...\n")
	while (true) {
		if (!readCurveElement()) break;
		curve_list_size++;
	}
	LOG_PRINT(logfile, "   ... done. Curves: %d\n", curve_list_size)

	// get axisbreak list
	unsigned int axisbreak_list_size = 0;

	LOG_PRINT(logfile, "   Reading Axis breaks ...\n")
	while (true) {
		if (!readAxisBreakElement()) break;
		axisbreak_list_size++;
	}
	LOG_PRINT(logfile, "   ... done. Axis breaks: %d\n", axisbreak_list_size)

	// get x axisparameter list
	unsigned int axispar_x_list_size = 0;

	LOG_PRINT(logfile, "   Reading x-Axis parameters ...\n")
	while (true) {
		if (!readAxisParameterElement(1)) break;
		axispar_x_list_size++;
	}
	LOG_PRINT(logfile, "   ... done. x-Axis parameters: %d\n", axispar_x_list_size)

	// get y axisparameter list
	unsigned int axispar_y_list_size = 0;

	LOG_PRINT(logfile, "   Reading y-Axis parameters ...\n")
	while (true) {
		if (!readAxisParameterElement(2)) break;
		axispar_y_list_size++;
	}
	LOG_PRINT(logfile, "   ... done. y-Axis parameters: %d\n", axispar_y_list_size)

	// get z axisparameter list
	unsigned int axispar_z_list_size = 0;

	LOG_PRINT(logfile, "   Reading z-Axis parameters ...\n")
	while (true) {
		if (!readAxisParameterElement(3)) break;
		axispar_z_list_size++;
	}
	LOG_PRINT(logfile, "   ... done. z-Axis parameters: %d\n", axispar_z_list_size)

	curpos = file.tellg();
	LOG_PRINT(logfile, "  layer ends at %d [0x%X]\n", curpos, curpos)

	return true;
}

unsigned int OriginAnyParser::readAnnotationList() {
	/* Purpose of this function is to allow recursive call for groups of annotation elements. */
	unsigned int annotation_list_size = 0;

	while (true) {
		if (!readAnnotationElement()) break;
		annotation_list_size++;
	}
	return annotation_list_size;
}

bool OriginAnyParser::readAnnotationElement() {
	/* get general info and details of an Annotation
	 * return true if an Annotation is found, otherwise return false */
	unsigned int ane_header_size = 0;
	unsigned long curpos = 0, anh_start = 0;

	// get annotation header size
	ane_header_size = readObjectSize();
	if (ane_header_size == 0) return false;

	curpos = file.tellg();
	anh_start = curpos;
	LOG_PRINT(logfile, "    Annotation found: header size %d [0x%X], starts at %d [0x%X]: ", ane_header_size, ane_header_size, curpos, curpos)
	string ane_header = readObjectAsString(ane_header_size);

	// get known info
	string name(41,0);
	name = ane_header.substr(0x46,41);
	LOG_PRINT(logfile, "%s\n", name.c_str())

	// go to end of annotation header
	file.seekg(anh_start+ane_header_size+1, ios_base::beg);

	// data of an annotation element is divided in three blocks
	// first block
	unsigned int ane_data_1_size = 0, andt1_start = 0;
	ane_data_1_size = readObjectSize();

	andt1_start = file.tellg();
	LOG_PRINT(logfile, "     block 1 size %d [0x%X] at %d [0x%X]\n", ane_data_1_size, ane_data_1_size, andt1_start, andt1_start)
	string andt1_data = readObjectAsString(ane_data_1_size);

	// TODO: get known info

	// go to end of first data block
	file.seekg(andt1_start+ane_data_1_size+1, ios_base::beg);

	// second block
	unsigned int ane_data_2_size = 0, andt2_start = 0;
	ane_data_2_size = readObjectSize();
	andt2_start = file.tellg();
	LOG_PRINT(logfile, "     block 2 size %d [0x%X] at %d [0x%X]\n", ane_data_2_size, ane_data_2_size, andt2_start, andt2_start)
	string andt2_data;

	// check for group of annotations
	if ((ane_data_1_size == 0x5e) and (ane_data_2_size == 0x04)) {
		unsigned int angroup_size = 0;
		curpos = file.tellg();
		LOG_PRINT(logfile, "  Annotation group found at %d [0x%X] ...\n", curpos, curpos)
		angroup_size = readAnnotationList();
		curpos = file.tellg();
		LOG_PRINT(logfile, "  ... group end at %d [0x%X]. Annotations: %d\n", curpos, curpos, angroup_size)
		andt2_data = string("");
	} else {
		andt2_data = readObjectAsString(ane_data_2_size);
		// TODO: get known info
		// go to end of second data block
		file.seekg(andt2_start+ane_data_2_size, ios_base::beg);
		if (ane_data_2_size > 0) file.seekg(1, ios_base::cur);
	}

	// third block
	unsigned int ane_data_3_size = 0, andt3_start = 0;
	ane_data_3_size = readObjectSize();

	andt3_start = file.tellg();
	LOG_PRINT(logfile, "     block 3 size %d [0x%X] at %d [0x%X]\n", ane_data_3_size, ane_data_3_size, andt3_start, andt3_start)
	string andt3_data = readObjectAsString(ane_data_3_size);

	curpos = file.tellg();
	LOG_PRINT(logfile, "    annotation ends at %d [0x%X]\n", curpos, curpos)

	// get annotation info
	getAnnotationProperties(ane_header, ane_header_size, andt1_data, ane_data_1_size, andt2_data, ane_data_2_size, andt3_data, ane_data_3_size);

	return true;
}

bool OriginAnyParser::readCurveElement() {
	/* get general info and details of a Curve
	 * return true if a Curve is found, otherwise return false */
	unsigned int cve_header_size = 0, cve_data_size = 0;
	unsigned long curpos = 0, cvh_start = 0, cvd_start = 0;

	// get curve header size
	cve_header_size = readObjectSize();
	if (cve_header_size == 0) return false;

	curpos = file.tellg();
	cvh_start = curpos;
	LOG_PRINT(logfile, "    Curve: header size %d [0x%X], starts at %d [0x%X], ", cve_header_size, cve_header_size, curpos, curpos)
	string cve_header = readObjectAsString(cve_header_size);

	// TODO: get known info from curve header
	string name = cve_header.substr(0x12,12);

	// go to end of header, get curve data size
	file.seekg(cvh_start+cve_header_size+1, ios_base::beg);
	cve_data_size = readObjectSize();
	cvd_start = file.tellg();
	LOG_PRINT(logfile, "data size %d [0x%X], from %d [0x%X]", cve_data_size, cve_data_size, cvd_start, cvd_start)
	string cve_data = readObjectAsString(cve_data_size);

	// TODO: get known info from curve data

	// go to end of data
	file.seekg(cvd_start+cve_data_size, ios_base::beg);
	if (cve_data_size > 0) file.seekg(1, ios_base::cur);

	curpos = file.tellg();
	LOG_PRINT(logfile, "to %d [0x%X]: %s\n", curpos, curpos, name.c_str())

	// get curve (or column) info
	getCurveProperties(cve_header, cve_header_size, cve_data, cve_data_size);

	return true;
}

bool OriginAnyParser::readAxisBreakElement() {
	/* get info of Axis breaks
	 * return true if an Axis break, otherwise return false */
	unsigned int abe_data_size = 0;
	unsigned long curpos = 0, abd_start = 0;

	// get axis break data size
	abe_data_size = readObjectSize();
	if (abe_data_size == 0) return false;

	curpos = file.tellg();
	abd_start = curpos;
	string abd_data = readObjectAsString(abe_data_size);

	// get known info

	// go to end of axis break data
	file.seekg(abd_start+abe_data_size+1, ios_base::beg);

	return true;
}

bool OriginAnyParser::readAxisParameterElement(unsigned int naxis) {
	/* get info of Axis parameters for naxis-axis (x,y,z) = (1,2,3)
	 * return true if an Axis break is found, otherwise return false */
	unsigned int ape_data_size = 0;
	unsigned long curpos = 0, apd_start = 0;

	// get axis break data size
	ape_data_size = readObjectSize();
	if (ape_data_size == 0) return false;

	curpos = file.tellg();
	apd_start = curpos;
	string apd_data = readObjectAsString(ape_data_size);

	// get known info

	// go to end of axis break data
	file.seekg(apd_start+ape_data_size+1, ios_base::beg);

	return true;
}

bool OriginAnyParser::readParameterElement() {
	// get parameter name
	unsigned int par_start = 0, curpos = 0;
	string par_name;
	char c;

	par_start = file.tellg();
	getline(file, par_name);
	if (par_name[0] == '\0') {
		unsigned int eof_parameters_mark = readObjectSize();
		return false;
	}
	LOG_PRINT(logfile, " %s:", par_name.c_str())
	// get value
	double value;
	file >> value;
	LOG_PRINT(logfile, " %g\n", value)
	// read the '\n'
	file >> c;
	if (c != '\n') {
		LOG_PRINT(logfile, "Wrong delimiter %c at %d [0x%X]\n", c, (unsigned long)file.tellg(), (unsigned long)file.tellg())
		exit(3);
	}

	return true;
}

bool OriginAnyParser::readNoteElement() {
	/* get info of Note windows, including "Results Log"
	 * return true if a Note window is found, otherwise return false */
	unsigned int nwe_header_size = 0, nwe_label_size = 0, nwe_contents_size = 0;
	unsigned long curpos = 0, nwh_start = 0, nwl_start = 0, nwc_start = 0;

	// get note header size
	nwe_header_size = readObjectSize();
	if (nwe_header_size == 0) return false;

	curpos = file.tellg();
	nwh_start = curpos;
	LOG_PRINT(logfile, "  Note window found: header size %d [0x%X], starts at %d [0x%X]\n", nwe_header_size, nwe_header_size, curpos, curpos)
	string nwe_header = readObjectAsString(nwe_header_size);

	// TODO: get known info from header

	// go to end of header
	file.seekg(nwh_start+nwe_header_size+1, ios_base::beg);

	// get label size
	nwe_label_size = readObjectSize();
	nwl_start = file.tellg();
	string nwe_label = readObjectAsString(nwe_label_size);
	LOG_PRINT(logfile, "  label at %d [0x%X]: %s\n", nwl_start, nwl_start, nwe_label.c_str())

	// go to end of label
	file.seekg(nwl_start+nwe_label_size, ios_base::beg);
	if (nwe_label_size > 0) file.seekg(1, ios_base::cur);

	// get contents size
	nwe_contents_size = readObjectSize();
	nwc_start = file.tellg();
	string nwe_contents = readObjectAsString(nwe_contents_size);
	LOG_PRINT(logfile, "  contents at %d [0x%X]: \n%s\n", nwc_start, nwc_start, nwe_contents.c_str())

	return true;
}

void OriginAnyParser::readProjectTree() {
	unsigned int pte_depth = 0;

	// first preamble size and data (usually 4)
	unsigned int pte_pre1_size = readObjectSize();
	string pte_pre1 = readObjectAsString(pte_pre1_size);

	// second preamble size and data (usually 16)
	unsigned int pte_pre2_size = readObjectSize();
	string pte_pre2 = readObjectAsString(pte_pre2_size);

	// root element and children
	unsigned int rootfolder = readFolderTree(pte_depth);

	// epilogue (should be zero)
	unsigned int pte_post_size = readObjectSize();

	return;
}

unsigned int OriginAnyParser::readFolderTree(unsigned int depth) {
	unsigned int fle_header_size = 0, fle_eofh_size = 0, fle_name_size = 0, fle_prop_size = 0;
	unsigned long curpos = 0;

	// folder header size, data, end mark
	fle_header_size = readObjectSize();
	string fle_header = readObjectAsString(fle_header_size);
	fle_eofh_size = readObjectSize(); // (usually 0)

	// folder name size
	fle_name_size = readObjectSize();
	curpos = file.tellg();
	string fle_name = readObjectAsString(fle_name_size);
	LOG_PRINT(logfile, "Folder name at %d [0x%X]: %s\n", curpos, curpos, fle_name.c_str());

	// additional properties
	fle_prop_size = readObjectSize();
	for (unsigned int i = 0; i < fle_prop_size; i++) {
		unsigned int obj_size = readObjectSize();
		string obj_data = readObjectAsString(obj_size);
	}

	// file entries
	unsigned int number_of_files_size = 0;

	number_of_files_size = readObjectSize(); // should be 4 as number_of_files is an integer
	curpos = file.tellg();
	LOG_PRINT(logfile, "Number of files at %d [0x%X] ", curpos, curpos);
	string fle_nfiles = readObjectAsString(number_of_files_size);

	istringstream stmp(ios_base::binary);
	stmp.str(fle_nfiles);
	unsigned int number_of_files = 0;
	stmp.read(reinterpret_cast<char *>(&number_of_files), number_of_files_size);
	LOG_PRINT(logfile, "%d\n", number_of_files)

	for (unsigned int i=0; i < number_of_files; i++) {
		readProjectLeave();
	}

	// subfolder entries
	unsigned int number_of_folders_size = 0;

	number_of_folders_size = readObjectSize(); // should be 4 as number_of_subfolders is an integer
	curpos = file.tellg();
	LOG_PRINT(logfile, "Number of subfolders at %d [0x%X] ", curpos, curpos);
	string fle_nfolders = readObjectAsString(number_of_folders_size);

	stmp.str(fle_nfolders);
	unsigned int number_of_folders = 0;
	stmp.read(reinterpret_cast<char *>(&number_of_folders), number_of_folders_size);
	LOG_PRINT(logfile, "%d\n", number_of_folders)

	for (unsigned int i=0; i < number_of_folders; i++) {
		depth++;
		unsigned int subfolder = readFolderTree(depth);
		depth--;
	}

	return number_of_files;
}

void OriginAnyParser::readProjectLeave() {
	unsigned long curpos = 0;

	// preamble size (usually 0) and data
	unsigned int ptl_pre_size = readObjectSize();
	string ptl_pre = readObjectAsString(ptl_pre_size);

	// file data size (usually 8) and data
	unsigned int ptl_data_size = readObjectSize();
	curpos = file.tellg();
	string ptl_data = readObjectAsString(ptl_data_size);

	// get info from file data
	istringstream stmp(ios_base::binary);
	stmp.str(ptl_data);
	unsigned int file_type = 0, file_object_id = 0;
	GET_INT(stmp, file_type);
	GET_INT(stmp, file_object_id);

	LOG_PRINT(logfile, "File at %d [0x%X], type %d, object_id %d\n", curpos, curpos, file_type, file_object_id)

	// epilogue (should be zero)
	unsigned int ptl_post_size = readObjectSize();

	return;
}

void OriginAnyParser::readAttachmentList() {
	/* Attachments are divided in two groups (which can be empty)
	 first group is preceeded by two integers: 4096 (0x1000) and number_of_attachments followed as usual by a '\n' mark
	 second group is a series of (header, name, data) triplets without the '\n' mark.
	*/

	// figure out if first group is not empty. In this case we will read integer=8 at current file position
	unsigned int att_1st_empty = 0;
	file >> att_1st_empty;
	file.seekg(-4, ios_base::cur);

	istringstream stmp(ios_base::binary);
	string att_header;
	unsigned long curpos = 0;
	if (att_1st_empty == 8) {
		// first group
		unsigned int att_list1_size = 0;

		// get two integers
		// next line fails if first attachment group is empty: readObjectSize exits as there is no '\n' after 4 bytes for uint
		att_list1_size = readObjectSize(); // should be 8 as we expect two integer values
		curpos = file.tellg();
		string att_list1 = readObjectAsString(att_list1_size);
		LOG_PRINT(logfile, "First attachment group at %d [0x%X]", curpos, curpos)

		stmp.str(att_list1);

		unsigned int att_mark = 0, number_of_atts = 0, iattno = 0, att_data_size = 0;
		GET_INT(stmp, att_mark) // should be 4096
		GET_INT(stmp, number_of_atts)
		LOG_PRINT(logfile, " with %d attachments.\n", number_of_atts)

		for (unsigned int i=0; i < number_of_atts; i++) {
			/* Header is a group of 7 integers followed by \n
			1st  attachment mark (4096: 0x00 0x10 0x00 0x00)
			2nd  attachment number ( <num_of_att)
			3rd  attachment size
			4th .. 7th ???
			*/
			// get header
			att_header = readObjectAsString(7*4);
			stmp.str(att_header);
			GET_INT(stmp, att_mark) // should be 4096
			GET_INT(stmp, iattno)
			GET_INT(stmp, att_data_size)
			curpos = file.tellg();
			LOG_PRINT(logfile, "Attachment no %d (%d) at %d [0x%X], size %d\n", i, iattno, curpos, curpos, att_data_size)

			// get data
			string att_data = readObjectAsString(att_data_size);
			// even if att_data_size is zero, we get a '\n' mark
			if (att_data_size == 0) file.seekg(1, ios_base::cur);
		}
	} else {
		LOG_PRINT(logfile, "First attachment group is empty\n")
	}

	/* Second group is a series of (header, name, data) triplets
	   There is no number of attachments. It ends when we reach EOF. */
	curpos = file.tellg();
	LOG_PRINT(logfile, "Second attachment group starts at %d [0x%X], file size %d\n", curpos, curpos, d_file_size)
	/* Header is a group of 3 integers, with no '\n' at end
		1st attachment header+name size including itself
		2nd attachment type (0x59 0x04 0xCA 0x7F for excel workbooks)
		3rd size of data */

	// get header
	att_header = string(12,0);
	while (true) {
		// check for eof
		if ((file.tellg() == d_file_size) || (file.eof())) break;
		// cannot use readObjectAsString: there is no '\n' at end
		file.read(reinterpret_cast<char*>(&att_header[0]), 12);

		if (file.gcount() != 12) break;
		// get header size, type and data size
		unsigned int att_header_size=0, att_type=0, att_size=0;
		stmp.str(att_header);
		GET_INT(stmp, att_header_size)
		GET_INT(stmp, att_type)
		GET_INT(stmp, att_size)

		// get name and data
		unsigned int name_size = att_header_size - 3*4;
		string att_name = string(name_size, 0);
		file.read(&att_name[0], name_size);
		curpos = file.tellg();
		string att_data = string(att_size, 0);
		file.read(&att_data[0], att_size);
		LOG_PRINT(logfile, "attachment at %d [0x%X], type 0x%X, size %d [0x%X]: %s\n", curpos, curpos, att_type, att_size, att_size, att_name.c_str())
	}

	return;
}

bool OriginAnyParser::getColumnInfoAndData(string col_header, unsigned int col_header_size, string col_data, unsigned int col_data_size) {
	istringstream stmp(ios_base::binary);
	static unsigned int dataIndex=0;
	short data_type;
	char data_type_u;
	char valuesize;
	string name(25,0), column_name;

	stmp.str(col_header.substr(0x16));
	GET_SHORT(stmp, data_type);

	data_type_u = col_header[0x3F];
	valuesize = col_header[0x3D];
	if(valuesize == 0) {
		LOG_PRINT(logfile, "	WARNING : found strange valuesize of %d\n", (int)valuesize);
		valuesize = 10;
	}

	name = col_header.substr(0x58,25).c_str();
	string::size_type colpos = name.find_last_of("_");

	if(colpos != string::npos){
		column_name = name.substr(colpos + 1);
		name.resize(colpos);
	}

	LOG_PRINT(logfile, "\n  data_type 0x%.4X, data_type_u 0x%.2X, valuesize %d [0x%X], %s [%s]\n", data_type, data_type_u, valuesize, valuesize, name.c_str(), column_name.c_str());

	unsigned short signature;
	if (col_header_size > 0x72) {
		stmp.str(col_header.substr(0x71));
		GET_SHORT(stmp, signature);

		int total_rows, first_row, last_row;
		stmp.str(col_header.substr(0x19));
		GET_INT(stmp, total_rows);
		GET_INT(stmp, first_row);
		GET_INT(stmp, last_row);
		LOG_PRINT(logfile, "  total %d, first %d, last %d rows\n", total_rows, first_row, last_row)
	} else {
		LOG_PRINT(logfile, "  NOTE: alternative signature determination\n")
		signature = col_header[0x18];
	}
	LOG_PRINT(logfile, "  signature %d [0x%X], valuesize %d size %d ", signature, signature, valuesize, col_data_size)


	unsigned int current_col = 1;//, nr = 0, nbytes = 0;
	static unsigned int col_index = 0;
	unsigned int current_sheet = 0;
	int spread = 0;

	if (column_name.empty()) { // Matrix or function
		if (valuesize == col_data_size) { // only one row: Function
			functions.push_back(Function(name, dataIndex));
			++dataIndex;
			Origin::Function &f = functions.back();
			f.formula = toLowerCase(col_data.c_str());

			stmp.str(col_header.substr(0x0A));
			short t;
			GET_SHORT(stmp, t)
			if (t == 0x1194)
				f.type = Function::Polar;

			stmp.str(col_header.substr(0x21));
			GET_INT(stmp, f.totalPoints)
			GET_DOUBLE(stmp, f.begin)
			double d;
			GET_DOUBLE(stmp, d)
			f.end = f.begin + d*(f.totalPoints - 1);

			LOG_PRINT(logfile, "\n NEW FUNCTION: %s = %s", f.name.c_str(), f.formula.c_str());
			LOG_PRINT(logfile, ". Range [%g : %g], number of points: %d\n", f.begin, f.end, f.totalPoints);

		} else { // Matrix
			int mIndex = -1;
			string::size_type pos = name.find_first_of("@");
			if (pos != string::npos){
				string sheetName = name;
				name.resize(pos);
				mIndex = findMatrixByName(name);
				if (mIndex != -1){
					LOG_PRINT(logfile, "\n  NEW MATRIX SHEET\n");
					matrixes[mIndex].sheets.push_back(MatrixSheet(sheetName, dataIndex));
				}
			} else {
				LOG_PRINT(logfile, "\n  NEW MATRIX\n");
				matrixes.push_back(Matrix(name));
				matrixes.back().sheets.push_back(MatrixSheet(name, dataIndex));
			}
			++dataIndex;
			getMatrixValues(col_data, col_data_size, data_type, data_type_u, valuesize, mIndex);
		}
	} else {
		if(speadSheets.size() == 0 || findSpreadByName(name) == -1) {
			LOG_PRINT(logfile, "\n  NEW SPREADSHEET\n");
			current_col = 1;
			speadSheets.push_back(SpreadSheet(name));
			spread = speadSheets.size() - 1;
			speadSheets.back().maxRows = 0;
			current_sheet = 0;
		} else {
			spread = findSpreadByName(name);
			current_col = speadSheets[spread].columns.size();
			if(!current_col)
				current_col = 1;
			++current_col;
		}
		speadSheets[spread].columns.push_back(SpreadColumn(column_name, dataIndex));
		speadSheets[spread].columns.back().colIndex = ++col_index;

		string::size_type sheetpos = speadSheets[spread].columns.back().name.find_last_of("@");
		if(sheetpos != string::npos){
			unsigned int sheet = strtol(column_name.substr(sheetpos + 1).c_str(), 0, 10);
			if( sheet > 1){
				speadSheets[spread].columns.back().name = column_name;

				if (current_sheet != (sheet - 1))
					current_sheet = sheet - 1;

				speadSheets[spread].columns.back().sheet = current_sheet;
				if (speadSheets[spread].sheets < sheet)
					speadSheets[spread].sheets = sheet;
			}
		}
		++dataIndex;
		LOG_PRINT(logfile, "  data index %d, valuesize %d, ", dataIndex, valuesize)

		unsigned int nr = col_data_size / valuesize;
		LOG_PRINT(logfile, "n. of rows = %d\n\n", nr)

		speadSheets[spread].maxRows<nr ? speadSheets[spread].maxRows=nr : 0;
		stmp.str(col_data);
		for(unsigned int i = 0; i < nr; ++i)
		{
			double value;
			if(valuesize <= 8)	// Numeric, Time, Date, Month, Day
			{
				GET_DOUBLE(stmp, value)
				if ((i < 5) or (i > (nr-5))) {
					LOG_PRINT(logfile, "%g ", value)
				} else if (i == 5) {
					LOG_PRINT(logfile, "... ")
				}
				speadSheets[spread].columns[(current_col-1)].data.push_back(value);
			}
			else if((data_type & 0x100) == 0x100) // Text&Numeric
			{
				unsigned char c = col_data[i*valuesize];
				stmp.seekg(i*valuesize+2, ios_base::beg);
				if(c != 1) //value
				{
					GET_DOUBLE(stmp, value);
					if ((i < 5) or (i > (nr-5))) {
						LOG_PRINT(logfile, "%g ", value)
					} else if (i == 5) {
						LOG_PRINT(logfile, "... ")
					}
					speadSheets[spread].columns[(current_col-1)].data.push_back(value);
				}
				else //text
				{
					string svaltmp = col_data.substr(i*valuesize+2, valuesize-2).c_str();
					// TODO: check if this test is still needed
					if(svaltmp.find(0x0E) != string::npos) { // try find non-printable symbol - garbage test
						svaltmp = string();
						LOG_PRINT(logfile, "Non printable symbol found, place 1 for i=%d\n", i)
					}
					if ((i < 5) or (i > (nr-5))) {
						LOG_PRINT(logfile, "\"%s\" ", svaltmp.c_str())
					} else if (i == 5) {
						LOG_PRINT(logfile, "... ")
					}
					speadSheets[spread].columns[(current_col-1)].data.push_back(svaltmp);
				}
			}
			else //text
			{
				string svaltmp = col_data.substr(i*valuesize, valuesize).c_str();
				// TODO: check if this test is still needed
				if(svaltmp.find(0x0E) != string::npos) { // try find non-printable symbol - garbage test
					svaltmp = string();
					LOG_PRINT(logfile, "Non printable symbol found, place 2 for i=%d\n", i)
				}
				if ((i < 5) or (i > (nr-5))) {
					LOG_PRINT(logfile, "\"%s\" ", svaltmp.c_str())
				} else if (i == 5) {
					LOG_PRINT(logfile, "... ")
				}
				speadSheets[spread].columns[(current_col-1)].data.push_back(svaltmp);
			}
		}
		LOG_PRINT(logfile, "\n\n")
	}

	return true;
}

void OriginAnyParser::getMatrixValues(string col_data, unsigned int col_data_size, short data_type, char data_type_u, char valuesize, int mIndex) {
	if (matrixes.empty())
		return;

	istringstream stmp;
	stmp.str(col_data);

	if (mIndex < 0)
		mIndex = matrixes.size() - 1;

	int size = col_data_size/valuesize;
	bool logValues = true;
	switch(data_type){
		case 0x6001://double
			for(unsigned int i = 0; i < size; ++i){
				double value;
				GET_DOUBLE(stmp, value)
				matrixes[mIndex].sheets.back().data.push_back((double)value);
			}
			break;
		case 0x6003://float
			for(unsigned int i = 0; i < size; ++i){
				float value;
				GET_FLOAT(stmp, value)
				matrixes[mIndex].sheets.back().data.push_back((double)value);
			}
			break;
		case 0x6801://int
			if (data_type_u == 8){//unsigned
				for(unsigned int i = 0; i < size; ++i){
					unsigned int value;
					GET_INT(stmp, value)
					matrixes[mIndex].sheets.back().data.push_back((double)value);
				}
			} else {
				for(unsigned int i = 0; i < size; ++i){
					int value;
					GET_INT(stmp, value)
					matrixes[mIndex].sheets.back().data.push_back((double)value);
				}
			}
			break;
		case 0x6803://short
			if (data_type_u == 8){//unsigned
				for(unsigned int i = 0; i < size; ++i){
					unsigned short value;
					GET_SHORT(stmp, value)
					matrixes[mIndex].sheets.back().data.push_back((double)value);
				}
			} else {
				for(unsigned int i = 0; i < size; ++i){
					short value;
					GET_SHORT(stmp, value)
					matrixes[mIndex].sheets.back().data.push_back((double)value);
				}
			}
			break;
		case 0x6821://char
			if (data_type_u == 8){//unsigned
				for(unsigned int i = 0; i < size; ++i){
					unsigned char value;
					value = col_data[i];
					matrixes[mIndex].sheets.back().data.push_back((double)value);
				}
			} else {
				for(unsigned int i = 0; i < size; ++i){
					char value;
					value = col_data[i];
					matrixes[mIndex].sheets.back().data.push_back((double)value);
				}
			}
			break;
		default:
			LOG_PRINT(logfile, "	UNKNOWN MATRIX DATATYPE: %02X SKIP DATA\n", data_type);
			matrixes.pop_back();
			logValues = false;
	}

	if (logValues){
		LOG_PRINT(logfile, "	FIRST 10 CELL VALUES: ");
		for(unsigned int i = 0; i < 10 && i < matrixes[mIndex].sheets.back().data.size(); ++i)
			LOG_PRINT(logfile, "%g\t", matrixes[mIndex].sheets.back().data[i]);
	}
}

void OriginAnyParser::getWindowProperties(Origin::Window& window, string wde_header, unsigned int wde_header_size) {
	window.objectID = objectIndex;
	++objectIndex;

	istringstream stmp;

	stmp.str(wde_header.substr(0x1B));
	GET_SHORT(stmp, window.frameRect.left)
	GET_SHORT(stmp, window.frameRect.top)
	GET_SHORT(stmp, window.frameRect.right)
	GET_SHORT(stmp, window.frameRect.bottom)

	char c = wde_header[0x32];

	if(c & 0x01)
		window.state = Window::Minimized;
	else if(c & 0x02)
		window.state = Window::Maximized;

	c = wde_header[0x69];

	if(c & 0x01)
		window.title = Window::Label;
	else if(c & 0x02)
		window.title = Window::Name;
	else
		window.title = Window::Both;

	window.hidden = (c & 0x08);
	if (window.hidden) {
		LOG_PRINT(logfile, "			WINDOW %d NAME : %s	is hidden\n", objectIndex, window.name.c_str());
	} else {
		LOG_PRINT(logfile, "			WINDOW %d NAME : %s	is not hidden\n", objectIndex, window.name.c_str());
	}

	if (wde_header_size > 0x82) {
		// only projects of version 6.0 and higher have these
		double creationDate, modificationDate;
		stmp.str(wde_header.substr(0x73));
		GET_DOUBLE(stmp, creationDate);
		window.creationDate = doubleToPosixTime(creationDate);
		GET_DOUBLE(stmp, modificationDate)
		window.modificationDate = doubleToPosixTime(modificationDate);
	}

	if(wde_header_size > 0xC3){
		window.label = wde_header.substr(0xC3).c_str();
		LOG_PRINT(logfile, "			WINDOW %d LABEL: %s\n", objectIndex, window.label.c_str());
	}

	if (imatrix != -1) { // additional properties for matrix windows
		unsigned char h = wde_header[0x29];
		matrixes[imatrix].activeSheet = h;
		if (wde_header_size > 0x86) {
			h = wde_header[0x87];
			matrixes[imatrix].header = (h == 194) ? Matrix::XY : Matrix::ColumnRow;
		}
	}
	if (igraph != -1) { // additional properties for graph/layout windows
		stmp.str(wde_header.substr(0x23));
		GET_SHORT(stmp, graphs[igraph].width)
		GET_SHORT(stmp, graphs[igraph].height)

		unsigned char c = wde_header[0x38];
		graphs[igraph].connectMissingData = (c & 0x40);

		string templateName = wde_header.substr(0x45,20).c_str();
		graphs[igraph].templateName = templateName;
		if (templateName == "LAYOUT") graphs[igraph].isLayout = true;
	}
}

void OriginAnyParser::getLayerProperties(string lye_header, unsigned int lye_header_size) {
	istringstream stmp;

	if (ispread != -1) { // spreadsheet

		speadSheets[ispread].loose = false;

	} else if (imatrix != -1) { // matrix

		MatrixSheet& sheet = matrixes[imatrix].sheets[ilayer];

		unsigned short width = 8;
		stmp.str(lye_header.substr(0x27));
		GET_SHORT(stmp, width)
		if (width == 0) width = 8;
		sheet.width = width;

		stmp.str(lye_header.substr(0x2B));
		GET_SHORT(stmp, sheet.columnCount)

		stmp.str(lye_header.substr(0x52));
		GET_SHORT(stmp, sheet.rowCount)

		unsigned char view = lye_header[0x71];
		if (view != 0x32 && view != 0x28){
			sheet.view = MatrixSheet::ImageView;
		} else {
			sheet.view = MatrixSheet::DataView;
		}

		if (lye_header_size > 0xD2) {
			sheet.name = lye_header.substr(0xD2,32).c_str();
		}

	} else if (iexcel != -1) { // excel

		excels[iexcel].loose = false;

	} else { // graph
		graphs[igraph].layers.push_back(GraphLayer());
		GraphLayer& glayer = graphs[igraph].layers[ilayer];

		stmp.str(lye_header.substr(0x0F));
		GET_DOUBLE(stmp, glayer.xAxis.min);
		GET_DOUBLE(stmp, glayer.xAxis.max);
		GET_DOUBLE(stmp, glayer.xAxis.step);

		glayer.xAxis.majorTicks = lye_header[0x2B];

		unsigned char g = lye_header[0x2D];
		glayer.xAxis.zeroLine = (g & 0x80);
		glayer.xAxis.oppositeLine = (g & 0x40);

		glayer.xAxis.minorTicks = lye_header[0x37];
		glayer.xAxis.scale = lye_header[0x38];

		stmp.str(lye_header.substr(0x3A));
		GET_DOUBLE(stmp, glayer.yAxis.min);
		GET_DOUBLE(stmp, glayer.yAxis.max);
		GET_DOUBLE(stmp, glayer.yAxis.step);

		glayer.yAxis.majorTicks = lye_header[0x56];

		g = lye_header[0x58];
		glayer.yAxis.zeroLine = (g & 0x80);
		glayer.yAxis.oppositeLine = (g & 0x40);

		glayer.yAxis.minorTicks = lye_header[0x62];
		glayer.yAxis.scale = lye_header[0x63];

		g = lye_header[0x68];
		glayer.gridOnTop = (g & 0x04);
		glayer.exchangedAxes = (g & 0x40);

		stmp.str(lye_header.substr(0x71));
		GET_SHORT(stmp, glayer.clientRect.left)
		GET_SHORT(stmp, glayer.clientRect.top)
		GET_SHORT(stmp, glayer.clientRect.right)
		GET_SHORT(stmp, glayer.clientRect.bottom)

		unsigned char border = lye_header[0x89];
		glayer.borderType = (BorderType)(border >= 0x80 ? border-0x80 : None);

		if (lye_header_size > 0x107)
			glayer.backgroundColor = getColor(lye_header.substr(0x105,4));

	}
}

Origin::Color OriginAnyParser::getColor(string strbincolor) {
	/* decode a color value from a 4 byte binary string */
	Origin::Color result;
	unsigned char sbincolor[4];
	for (int i=0; i < 4; i++) {
		sbincolor[i] = strbincolor[i];
	}
	switch(sbincolor[3]) {
		case 0:
			if(sbincolor[0] < 0x64) {
				result.type = Origin::Color::Regular;
				result.regular = sbincolor[0];
			} else {
				switch(sbincolor[2]) {
					case 0:
						result.type = Origin::Color::Indexing;
						break;
					case 0x40:
						result.type = Origin::Color::Mapping;
						break;
					case 0x80:
						result.type = Origin::Color::RGB;
						break;
				}
				result.column = sbincolor[0] - 0x64;
			}
			break;
		case 1:
			result.type = Origin::Color::Custom;
			for(int i = 0; i < 3; ++i)
				result.custom[i] = sbincolor[i];
			break;
		case 0x20:
			result.type = Origin::Color::Increment;
			result.starting = sbincolor[1];
			break;
		case 0xFF:
			if(sbincolor[0] == 0xFC)
				result.type = Origin::Color::None;
			else if(sbincolor[0] == 0xF7)
				result.type = Origin::Color::Automatic;
			else {
				result.type = Origin::Color::Regular;
				result.regular = sbincolor[0];
			}
			break;
		default:
			result.type = Origin::Color::Regular;
			result.regular = sbincolor[0];
			break;
	}
	return result;
}

void OriginAnyParser::getAnnotationProperties(string anhd, unsigned int anhdsz, string andt1, unsigned int andt1sz, string andt2, unsigned int andt2sz, string andt3, unsigned int andt3sz) {
	istringstream stmp;

	if (ispread != -1) {

		string sec_name = anhd.substr(0x46,41).c_str();
		int col_index = findColumnByName(ispread, sec_name);
		if (col_index != -1){ //check if it is a formula
			speadSheets[ispread].columns[col_index].command = andt1.c_str();
			LOG_PRINT(logfile, "				Column: %s has formula: %s\n", sec_name.c_str(), speadSheets[ispread].columns[col_index].command.c_str())
		}

	} else if (imatrix != -1) {

		MatrixSheet& sheet = matrixes[imatrix].sheets[ilayer];
		string sec_name = anhd.substr(0x46,41).c_str();

		stmp.str(andt1.c_str());
		if (sec_name == "MV") {
			sheet.command = andt1.c_str();
		} else if (sec_name == "Y2") {
			stmp >> sheet.coordinates[0];
		} else if (sec_name == "X2") {
			stmp >> sheet.coordinates[1];
		} else if (sec_name == "Y1") {
			stmp >> sheet.coordinates[2];
		} else if (sec_name == "X1") {
			stmp >> sheet.coordinates[3];
		} else if (sec_name == "COLORMAP") {
			// TODO: implement getColorMap (no example available)
			// sheet.colorMap = getColorMap(andt1, andt2, andt3)
		}

	} else if (iexcel != -1) {

		string sec_name = anhd.substr(0x46,41).c_str();
		int col_index = findExcelColumnByName(iexcel, ilayer, sec_name);
		if (col_index != -1){ //check if it is a formula
			excels[iexcel].sheets[ilayer].columns[col_index].command = andt1.c_str();
		}

	} else {

		GraphLayer& glayer = graphs[igraph].layers[ilayer];
		string sec_name = anhd.substr(0x46,41).c_str();

		Rect r;
		stmp.str(anhd.substr(0x03));
		GET_SHORT(stmp, r.left)
		GET_SHORT(stmp, r.top)
		GET_SHORT(stmp, r.right)
		GET_SHORT(stmp, r.bottom)

		unsigned char attach = anhd[0x28];
		unsigned char border = anhd[0x29];

		Color color = getColor(anhd.substr(0x33,4));

		if (sec_name == "PL") glayer.yAxis.formatAxis[0].prefix = andt1.c_str();
		if (sec_name == "PR") glayer.yAxis.formatAxis[1].prefix = andt1.c_str();
		if (sec_name == "PB") glayer.xAxis.formatAxis[0].prefix = andt1.c_str();
		if (sec_name == "PT") glayer.xAxis.formatAxis[1].prefix = andt1.c_str();
		if (sec_name == "SL") glayer.yAxis.formatAxis[0].suffix = andt1.c_str();
		if (sec_name == "SR") glayer.yAxis.formatAxis[1].suffix = andt1.c_str();
		if (sec_name == "SB") glayer.xAxis.formatAxis[0].suffix = andt1.c_str();
		if (sec_name == "ST") glayer.xAxis.formatAxis[1].suffix = andt1.c_str();
		if (sec_name == "OL") glayer.yAxis.formatAxis[0].factor = andt1.c_str();
		if (sec_name == "OR") glayer.yAxis.formatAxis[1].factor = andt1.c_str();
		if (sec_name == "OB") glayer.xAxis.formatAxis[0].factor = andt1.c_str();
		if (sec_name == "OT") glayer.xAxis.formatAxis[1].factor = andt1.c_str();

		unsigned char type = andt1[0x00];
		LineVertex begin, end;
		/* OriginNNNParser identify line/arrow annotation by checking size of andt1
		   Origin410: 21||24; Origin 500: 24; Origin 610: 24||96; Origin700 and higher: 120;
		   An alternative is to look at anhd[0x02]:
		     (0x21 for Circle/Rect, 0x22 for Line/Arrow, 0x23 for Polygon/Polyline)
		 */
		unsigned char ankind = anhd[0x02];
		if (ankind == 0x22) {//Line/Arrow
			if (attach == Origin::Scale) {
				if (type == 2) {
					stmp.str(andt1.substr(0x20));
					GET_DOUBLE(stmp, begin.x)
					GET_DOUBLE(stmp, end.x)
					stmp.str(andt1.substr(0x40));
					GET_DOUBLE(stmp, begin.y)
					GET_DOUBLE(stmp, end.y)
				} else if (type == 4) {//curved arrow: start point, 2 middle points and end point
					stmp.str(andt1.substr(0x20));
					GET_DOUBLE(stmp, begin.x)
					GET_DOUBLE(stmp, end.x)
					GET_DOUBLE(stmp, end.x)
					GET_DOUBLE(stmp, end.x)
					GET_DOUBLE(stmp, begin.y)
					GET_DOUBLE(stmp, end.y)
					GET_DOUBLE(stmp, end.y)
					GET_DOUBLE(stmp, end.y)
				}
			} else {
				short x1, x2, y1, y2;
				if (type == 2) {//straight line/arrow
					stmp.str(andt1.substr(0x01));
					GET_SHORT(stmp, x1)
					GET_SHORT(stmp, x2)
					stmp.seekg(4, ios_base::cur);
					GET_SHORT(stmp, y1)
					GET_SHORT(stmp, y2)
				} else if (type == 4) {//curved line/arrow has 4 points
					stmp.str(andt1.substr(0x01));
					GET_SHORT(stmp, x1)
					stmp.seekg(4, ios_base::cur);
					GET_SHORT(stmp, x2)
					GET_SHORT(stmp, y1)
					stmp.seekg(4, ios_base::cur);
					GET_SHORT(stmp, y2)
				}

				double dx = fabs(x2 - x1);
				double dy = fabs(y2 - y1);
				double minx = (x1 <= x2) ? x1 : x2;
				double miny = (y1 <= y2) ? y1 : y2;

				begin.x = (x1 == x2) ? r.left + 0.5*r.width() : r.left + (x1 - minx)/dx*r.width();
				end.x   = (x1 == x2) ? r.left + 0.5*r.width() : r.left + (x2 - minx)/dx*r.width();
				begin.y = (y1 == y2) ? r.top  + 0.5*r.height(): r.top + (y1 - miny)/dy*r.height();
				end.y   = (y1 == y2) ? r.top  + 0.5*r.height(): r.top + (y2 - miny)/dy*r.height();
			}
			unsigned char arrows = andt1[0x11];
			switch (arrows) {
				case 0:
					begin.shapeType = 0;
					end.shapeType = 0;
					break;
				case 1:
					begin.shapeType = 1;
					end.shapeType = 0;
					break;
				case 2:
					begin.shapeType = 0;
					end.shapeType = 1;
					break;
				case 3:
					begin.shapeType = 1;
					end.shapeType = 1;
					break;
			}
			if (andt1sz > 0x77) {
				begin.shapeType = andt1[0x60];
				unsigned int w = 0;
				stmp.str(andt1.substr(0x64));
				GET_INT(stmp, w)
				begin.shapeWidth = (double)w/500.0;
				GET_INT(stmp, w)
				begin.shapeLength = (double)w/500.0;

				end.shapeType = andt1[0x6C];
				stmp.str(andt1.substr(0x70));
				GET_INT(stmp, w)
				end.shapeWidth = (double)w/500.0;
				GET_INT(stmp, w)
				end.shapeLength = (double)w/500.0;
			}
		}
		//text properties
		short rotation;
		stmp.str(andt1.substr(0x02));
		GET_SHORT(stmp, rotation)
		unsigned char fontSize = andt1[0x4];
		unsigned char tab = andt1[0x0A];

		//line properties
		unsigned char lineStyle = andt1[0x12];
		unsigned short w1;
		stmp.str(andt1.substr(0x13));
		GET_SHORT(stmp, w1)
		double width = (double)w1/500.0;

		Figure figure;
		stmp.str(andt1.substr(0x05));
		GET_SHORT(stmp, w1)
		figure.width = (double)w1/500.0;
		figure.style = andt1[0x08];

		if (andt1sz > 0x4D) {
			figure.fillAreaColor = getColor(andt1.substr(0x42,4));
			stmp.str(andt1.substr(0x46));
			GET_SHORT(stmp, w1)
			figure.fillAreaPatternWidth = (double)w1/500.0;
			figure.fillAreaPatternColor = getColor(andt1.substr(0x4A,4));
			figure.fillAreaPattern = andt1[0x4E];
		}
		if (andt1sz > 0x56) {
			unsigned char h = andt1[0x57];
			figure.useBorderColor = (h == 0x10);
		}

		if (sec_name == "XB") {
			string text = andt2.c_str();
			glayer.xAxis.position = GraphAxis::Bottom;
			glayer.xAxis.formatAxis[0].label = TextBox(text, r, color, fontSize, rotation/10, tab, (BorderType)(border >= 0x80 ? border-0x80 : None), (Attach)attach);
		}
		else if (sec_name == "XT") {
			string text = andt2.c_str();
			glayer.xAxis.position = GraphAxis::Top;
			glayer.xAxis.formatAxis[1].label = TextBox(text, r, color, fontSize, rotation/10, tab, (BorderType)(border >= 0x80 ? border-0x80 : None), (Attach)attach);
		}
		else if (sec_name == "YL") {
			string text = andt2.c_str();
			glayer.yAxis.position = GraphAxis::Left;
			glayer.yAxis.formatAxis[0].label = TextBox(text, r, color, fontSize, rotation/10, tab, (BorderType)(border >= 0x80 ? border-0x80 : None), (Attach)attach);
		}
		else if (sec_name == "YR") {
			string text = andt2.c_str();
			glayer.yAxis.position = GraphAxis::Right;
			glayer.yAxis.formatAxis[1].label = TextBox(text, r, color, fontSize, rotation/10, tab, (BorderType)(border >= 0x80 ? border-0x80 : None), (Attach)attach);
		}
		else if (sec_name == "ZF") {
			string text = andt2.c_str();
			glayer.zAxis.position = GraphAxis::Front;
			glayer.zAxis.formatAxis[0].label = TextBox(text, r, color, fontSize, rotation/10, tab, (BorderType)(border >= 0x80 ? border-0x80 : None), (Attach)attach);
		}
		else if (sec_name == "ZB") {
			string text = andt2.c_str();
			glayer.zAxis.position = GraphAxis::Back;
			glayer.zAxis.formatAxis[1].label = TextBox(text, r, color, fontSize, rotation/10, tab, (BorderType)(border >= 0x80 ? border-0x80 : None), (Attach)attach);
		}
		else if (sec_name == "3D") {
			stmp.str(andt2);
			GET_DOUBLE(stmp, glayer.zAxis.min)
			GET_DOUBLE(stmp, glayer.zAxis.max)
			GET_DOUBLE(stmp, glayer.zAxis.step)
			glayer.zAxis.majorTicks = andt2[0x1C];
			glayer.zAxis.minorTicks = andt2[0x28];
			glayer.zAxis.scale = andt2[0x29];

			stmp.str(andt2.substr(0x5A));
			GET_FLOAT(stmp, glayer.xAngle)
			GET_FLOAT(stmp, glayer.yAngle)
			GET_FLOAT(stmp, glayer.zAngle)

			stmp.str(andt2.substr(0x218));
			GET_FLOAT(stmp, glayer.xLength)
			GET_FLOAT(stmp, glayer.yLength)
			GET_FLOAT(stmp, glayer.zLength)
			glayer.xLength /= 23.0;
			glayer.yLength /= 23.0;
			glayer.zLength /= 23.0;

			glayer.orthographic3D = (andt2[0x240] != 0);
		}
		else if (sec_name == "Legend") {
			string text = andt2.c_str();
			glayer.legend = TextBox(text, r, color, fontSize, rotation/10, tab, (BorderType)(border >= 0x80 ? border-0x80 : None), (Attach)attach);
		}
		else if (sec_name == "__BCO2") { // histogram
			stmp.str(andt2.substr(0x10));
			GET_DOUBLE(stmp, glayer.histogramBin)
			stmp.str(andt2.substr(0x20));
			GET_DOUBLE(stmp, glayer.histogramEnd)
			GET_DOUBLE(stmp, glayer.histogramBegin)

			// TODO: check if 0x5E is right (obtained from anhdsz-0x46+93-andt1sz = 111-70+93-40 = 94)
			glayer.percentile.p1SymbolType = andt2[0x5E];
			glayer.percentile.p99SymbolType = andt2[0x5F];
			glayer.percentile.meanSymbolType = andt2[0x60];
			glayer.percentile.maxSymbolType = andt2[0x61];
			glayer.percentile.minSymbolType = andt2[0x62];

			// 0x9F = 0x5E+65
			glayer.percentile.labels = andt2[0x9F];
			// 0x6B = 0x5E+106-93 = 107
			glayer.percentile.whiskersRange = andt2[0x6B];
			glayer.percentile.boxRange = andt2[0x6C];
			// 0x8e = 0x5E+141-93 = 142
			glayer.percentile.whiskersCoeff = andt2[0x8e];
			glayer.percentile.boxCoeff = andt2[0x8f];
			unsigned char h = andt2[0x90];
			glayer.percentile.diamondBox = (h == 0x82) ? true : false;
			// 0xCB = 0x5E+109 = 203
			stmp.str(andt2.substr(0xCB));
			GET_SHORT(stmp, glayer.percentile.symbolSize)
			glayer.percentile.symbolSize = glayer.percentile.symbolSize/2 + 1;
			// 0x101 = 0x5E+163
			glayer.percentile.symbolColor = getColor(andt2.substr(0x101,4));
			glayer.percentile.symbolFillColor = getColor(andt2.substr(0x105,4));
		}
		else if (sec_name == "_206") { // box plot labels
		}
		else if (sec_name == "VLine") {
			stmp.str(andt1.substr(0x0A));
			double start;
			GET_DOUBLE(stmp, start)
			stmp.str(andt1.substr(0x1A));
			double width;
			GET_DOUBLE(stmp, start)
			glayer.vLine = start + 0.5*width;
			glayer.imageProfileTool = 2;
		}
		else if (sec_name == "HLine") {
			stmp.str(andt1.substr(0x12));
			double start;
			GET_DOUBLE(stmp, start)
			stmp.str(andt1.substr(0x22));
			double width;
			GET_DOUBLE(stmp, start)
			glayer.hLine = start + 0.5*width;
			glayer.imageProfileTool = 2;
		}
		else if (sec_name == "vline") {
			stmp.str(andt1.substr(0x20));
			GET_DOUBLE(stmp, glayer.vLine)
			glayer.imageProfileTool = 1;
		}
		else if (sec_name == "hline") {
			stmp.str(andt1.substr(0x40));
			GET_DOUBLE(stmp, glayer.hLine)
			glayer.imageProfileTool = 1;
		}
		else if (sec_name == "ZCOLORS") {
			glayer.isXYY3D = true;
		}
		else if (sec_name == "SPECTRUM1") {
			glayer.isXYY3D = false;
			glayer.colorScale.visible = true;
			unsigned char h = andt1[0x18];
			glayer.colorScale.reverseOrder = h;
			stmp.str(andt1.substr(0x20));
			GET_SHORT(stmp, glayer.colorScale.colorBarThickness)
			GET_SHORT(stmp, glayer.colorScale.labelGap)
			glayer.colorScale.labelsColor = getColor(andt1.substr(0x5C,4));
		}
		else if (sec_name == "&0") {
			glayer.isWaterfall = true;
			string text = andt1.c_str();
			string::size_type commaPos = text.find_first_of(",");
			stmp.str(text.substr(0,commaPos));
			stmp >> glayer.xOffset;
			stmp.str(text.substr(commaPos+1));
			stmp >> glayer.yOffset;
		}
		/* OriginNNNParser identify text, circle, rectangle and bitmap annotation by checking size of andt1:
		             text/pie text          rectangle/circle       line            bitmap
		   Origin410: 22                    0xA(10)                21/24           38
		   Origin500: 22                    0xA(10)                24              40
		   Origin610: 22                    0xA(10)                24/96           40
		   Origin700:                       0x5E(94)               120             0x28(40)
		   Origin750: 0x3E(62)/78           0x5E(94)              0x78(120)        0x28(40)
		   Origin850: 0x3E(62)/78           0x5E(94)              0x78(120)        0x28(40)
		   An alternative is to look at anhd[0x02]:
		     (0x00 for Text, 0x21 for Circle/Rect, 0x22 for Line/Arrow, 0x23 for Polygon/Polyline)
		*/
		else if ((ankind == 0x0) && (sec_name != "DelData")) { // text
			string text = andt2.c_str();
			if (sec_name.substr(0,3) == "PIE")
				glayer.pieTexts.push_back(TextBox(text, r, color, fontSize, rotation/10, tab, (BorderType)(border >= 0x80 ? border-0x80 : None), (Attach)attach));
			else
				glayer.texts.push_back(TextBox(text, r, color, fontSize, rotation/10, tab, (BorderType)(border >= 0x80 ? border-0x80 : None), (Attach)attach));
		}
		else if (ankind == 0x21) { // rectangle & circle
			switch (type) { // type = andt1[0x00]
				case 0:
				case 1:
					figure.type = Figure::Rectangle;
					break;
				case 2:
				case 3:
					figure.type = Figure::Circle;
					break;
			}
			figure.clientRect = r;
			figure.attach = (Attach)attach;
			figure.color = color;

			glayer.figures.push_back(figure);
		}
		else if ((ankind == 0x22) && (sec_name != "sLine") && (sec_name != "sline")) { // line/arrow
			glayer.lines.push_back(Line());
			Line& line(glayer.lines.back());
			line.color = color;
			line.clientRect = r;
			line.attach = (Attach)attach;
			line.width = width;
			line.style = lineStyle;
			line.begin = begin;
			line.end = end;
		}
		else if (andt1sz == 40) { // bitmap
			if (type == 4) { // type = andt1[0x00]
				unsigned long filesize = andt2sz + 14;
				glayer.bitmaps.push_back(Bitmap());
				Bitmap& bitmap(glayer.bitmaps.back());
				bitmap.clientRect = r;
				bitmap.attach = (Attach)attach;
				bitmap.size = filesize;
				bitmap.borderType = (BorderType)(border >= 0x80 ? border-0x80 : None);
				bitmap.data = new unsigned char[filesize];
				unsigned char* data = bitmap.data;
				//add Bitmap header
				memcpy(data, "BM", 2);
				data += 2;
				memcpy(data, &filesize, 4);
				data += 4;
				unsigned int d = 0;
				memcpy(data, &d, 4);
				data += 4;
				d = 0x36;
				memcpy(data, &d, 4);
				data += 4;
				memcpy(data, andt2.c_str(), andt2sz);
			} else if (type == 6) {
				// TODO check if 0x5E is right (obtained from anhdsz-0x46+93-andt1sz = 111-70+93-40 = 94)
				string gname = andt2.substr(0x5E).c_str();
				glayer.bitmaps.push_back(Bitmap(gname));
				Bitmap& bitmap(glayer.bitmaps.back());
				bitmap.clientRect = r;
				bitmap.attach = (Attach)attach;
				bitmap.size = 0;
				bitmap.borderType = (BorderType)(border >= 0x80 ? border-0x80 : None);
			}
		}

	}
	return;
}

void OriginAnyParser::getCurveProperties(string cvehd, unsigned int cvehdsz, string cvedt, unsigned int cvedtsz) {
	istringstream stmp;

	if (ispread != -1) { // spreadsheet: curves are columns

		// TODO: check that spreadsheet columns are stored in proper order
		// vector<SpreadColumn> header;
		unsigned char c = cvehd[0x11];
		string name = cvehd.substr(0x12).c_str();
		unsigned short width = 0;
		stmp.str(cvehd.substr(0x4A));
		GET_SHORT(stmp, width)
		int col_index = findColumnByName(ispread, name);
		if (col_index != -1) {
			if (speadSheets[ispread].columns[col_index].name != name)
				speadSheets[ispread].columns[col_index].name = name;

			SpreadColumn::ColumnType type;
			switch(c){
				case 3:
					type = SpreadColumn::X;
					break;
				case 0:
					type = SpreadColumn::Y;
					break;
				case 5:
					type = SpreadColumn::Z;
					break;
				case 6:
					type = SpreadColumn::XErr;
					break;
				case 2:
					type = SpreadColumn::YErr;
					break;
				case 4:
					type = SpreadColumn::Label;
					break;
				default:
					type = SpreadColumn::NONE;
					break;
			}
			speadSheets[ispread].columns[col_index].type = type;

			width /= 0xA;
			if(width == 0) width = 8;
			speadSheets[ispread].columns[col_index].width = width;
			unsigned char c1 = cvehd[0x1E];
			unsigned char c2 = cvehd[0x1F];
			switch (c1) {
				case 0x00: // Numeric	   - Dec1000
				case 0x09: // Text&Numeric - Dec1000
				case 0x10: // Numeric	   - Scientific
				case 0x19: // Text&Numeric - Scientific
				case 0x20: // Numeric	   - Engeneering
				case 0x29: // Text&Numeric - Engeneering
				case 0x30: // Numeric	   - Dec1,000
				case 0x39: // Text&Numeric - Dec1,000
					speadSheets[ispread].columns[col_index].valueType = (c1%0x10 == 0x9) ? TextNumeric : Numeric;
					speadSheets[ispread].columns[col_index].valueTypeSpecification = c1 / 0x10;
					if (c2 >= 0x80) {
						speadSheets[ispread].columns[col_index].significantDigits = c2 - 0x80;
						speadSheets[ispread].columns[col_index].numericDisplayType = SignificantDigits;
					} else if (c2 > 0) {
						speadSheets[ispread].columns[col_index].decimalPlaces = c2 - 0x03;
						speadSheets[ispread].columns[col_index].numericDisplayType = DecimalPlaces;
					}
					break;
				case 0x02: // Time
					speadSheets[ispread].columns[col_index].valueType = Time;
					speadSheets[ispread].columns[col_index].valueTypeSpecification = c2 - 0x80;
					break;
				case 0x03: // Date
				case 0x33:
					speadSheets[ispread].columns[col_index].valueType = Date;
					speadSheets[ispread].columns[col_index].valueTypeSpecification= c2 - 0x80;
					break;
				case 0x31: // Text
					speadSheets[ispread].columns[col_index].valueType = Text;
					break;
				case 0x4: // Month
				case 0x34:
					speadSheets[ispread].columns[col_index].valueType = Month;
					speadSheets[ispread].columns[col_index].valueTypeSpecification = c2;
					break;
				case 0x5: // Day
				case 0x35:
					speadSheets[ispread].columns[col_index].valueType = Day;
					speadSheets[ispread].columns[col_index].valueTypeSpecification = c2;
					break;
				default: // Text
					speadSheets[ispread].columns[col_index].valueType = Text;
					break;
			}
			if (cvedtsz > 0) {
				speadSheets[ispread].columns[col_index].comment = cvedt.c_str();
			}
			// TODO: check that spreadsheet columns are stored in proper order
			// header.push_back(speadSheets[ispread].columns[col_index]);
		}
		// TODO: check that spreadsheet columns are stored in proper order
		// for (unsigned int i = 0; i < header.size(); i++)
		// 	speadSheets[spread].columns[i] = header[i];

	} else if (imatrix != -1) {

		MatrixSheet sheet = matrixes[imatrix].sheets[ilayer];
		unsigned char c1 = cvehd[0x1E];
		unsigned char c2 = cvehd[0x1F];

		sheet.valueTypeSpecification = c1/0x10;
		if (c2 >= 0x80) {
			sheet.significantDigits = c2-0x80;
			sheet.numericDisplayType = SignificantDigits;
		} else if (c2 > 0) {
			sheet.decimalPlaces = c2-0x03;
			sheet.numericDisplayType = DecimalPlaces;
		}

		matrixes[imatrix].sheets[ilayer] = sheet;

	} else if (iexcel != -1) {

		unsigned char c = cvehd[0x11];
		string name = cvehd.substr(0x12).c_str();
		unsigned short width = 0;
		stmp.str(cvehd.substr(0x4A));
		GET_SHORT(stmp, width)
		int col_index = findExcelColumnByName(iexcel, ilayer, name);
		if (col_index != -1) {
			SpreadColumn::ColumnType type;
			switch(c){
				case 3:
					type = SpreadColumn::X;
					break;
				case 0:
					type = SpreadColumn::Y;
					break;
				case 5:
					type = SpreadColumn::Z;
					break;
				case 6:
					type = SpreadColumn::XErr;
					break;
				case 2:
					type = SpreadColumn::YErr;
					break;
				case 4:
					type = SpreadColumn::Label;
					break;
				default:
					type = SpreadColumn::NONE;
					break;
			}
			excels[iexcel].sheets[ilayer].columns[col_index].type = type;
			width /= 0xA;
			if (width == 0) width = 8;
			excels[iexcel].sheets[ilayer].columns[col_index].width = width;

			unsigned char c1 = cvehd[0x1E];
			unsigned char c2 = cvehd[0x1F];
			switch (c1) {
				case 0x00: // Numeric	   - Dec1000
				case 0x09: // Text&Numeric - Dec1000
				case 0x10: // Numeric	   - Scientific
				case 0x19: // Text&Numeric - Scientific
				case 0x20: // Numeric	   - Engeneering
				case 0x29: // Text&Numeric - Engeneering
				case 0x30: // Numeric	   - Dec1,000
				case 0x39: // Text&Numeric - Dec1,000
					excels[iexcel].sheets[ilayer].columns[col_index].valueType = (c1%0x10 == 0x9) ? TextNumeric : Numeric;
					excels[iexcel].sheets[ilayer].columns[col_index].valueTypeSpecification = c1 / 0x10;
					if (c2 >= 0x80) {
						excels[iexcel].sheets[ilayer].columns[col_index].significantDigits = c2 - 0x80;
						excels[iexcel].sheets[ilayer].columns[col_index].numericDisplayType = SignificantDigits;
					} else if (c2 > 0) {
						excels[iexcel].sheets[ilayer].columns[col_index].decimalPlaces = c2 - 0x03;
						excels[iexcel].sheets[ilayer].columns[col_index].numericDisplayType = DecimalPlaces;
					}
					break;
				case 0x02: // Time
					excels[iexcel].sheets[ilayer].columns[col_index].valueType = Time;
					excels[iexcel].sheets[ilayer].columns[col_index].valueTypeSpecification = c2 - 0x80;
					break;
				case 0x03: // Date
					excels[iexcel].sheets[ilayer].columns[col_index].valueType = Date;
					excels[iexcel].sheets[ilayer].columns[col_index].valueTypeSpecification = c2 - 0x80;
					break;
				case 0x31: // Text
					excels[iexcel].sheets[ilayer].columns[col_index].valueType = Text;
					break;
				case 0x04: // Month
				case 0x34:
					excels[iexcel].sheets[ilayer].columns[col_index].valueType = Month;
					excels[iexcel].sheets[ilayer].columns[col_index].valueTypeSpecification = c2;
					break;
				case 0x05: // Day
				case 0x35:
					excels[iexcel].sheets[ilayer].columns[col_index].valueType = Day;
					excels[iexcel].sheets[ilayer].columns[col_index].valueTypeSpecification = c2;
					break;
				default: // Text
					excels[iexcel].sheets[ilayer].columns[col_index].valueType = Text;
					break;
			}
			if (cvedtsz > 0) {
				excels[iexcel].sheets[ilayer].columns[col_index].comment = cvedt.c_str();
			}
		}

	} else {

		GraphLayer& glayer = graphs[igraph].layers[ilayer];
		glayer.curves.push_back(GraphCurve());
		GraphCurve& curve(glayer.curves.back());

		unsigned char h = cvehd[0x26];
		curve.hidden = (h == 33);
		curve.type = cvehd[0x4C];
		if (curve.type == GraphCurve::XYZContour || curve.type == GraphCurve::Contour)
			glayer.isXYY3D = false;

		unsigned short w;
		stmp.str(cvehd.substr(0x04));
		GET_SHORT(stmp, w)
		pair<string, string> column = findDataByIndex(w-1);
		short nColY = w;
		if (column.first.size() > 0) {
			curve.dataName = column.first;
			if (glayer.is3D() || (curve.type == GraphCurve::XYZContour)) {
				curve.zColumnName = column.second;
			} else {
				curve.yColumnName = column.second;
			}
		}

		stmp.str(cvehd.substr(0x23));
		GET_SHORT(stmp, w)
		column = findDataByIndex(w-1);
		if (column.first.size() > 0) {
			curve.xDataName = (curve.dataName != column.first) ? column.first : "";
			if (glayer.is3D() || (curve.type == GraphCurve::XYZContour)) {
				curve.yColumnName = column.second;
			} else if (glayer.isXYY3D){
				curve.xColumnName = column.second;
			} else {
				curve.xColumnName = column.second;
			}
		}

		stmp.str(cvehd.substr(0x4D));
		GET_SHORT(stmp, w)
		column = findDataByIndex(w-1);
		if (column.first.size() > 0 && (glayer.is3D() || (curve.type == GraphCurve::XYZContour))) {
			curve.xColumnName = column.second;
			if (curve.dataName != column.first) {
				// graph X and Y from different tables
			}
		}

		if (glayer.is3D() || glayer.isXYY3D) graphs[igraph].is3D = true;

		curve.lineConnect = cvehd[0x11];
		curve.lineStyle = cvehd[0x12];
		curve.boxWidth = cvehd[0x14];

		stmp.str(cvehd.substr(0x15));
		GET_SHORT(stmp, w)
		curve.lineWidth = (double)w/500.0;

		stmp.str(cvehd.substr(0x19));
		GET_SHORT(stmp, w)
		curve.symbolSize = (double)w/500.0;

		h = cvehd[0x1C];
		curve.fillArea = (h==2);
		curve.fillAreaType = cvehd[0x1E];

		//text
		if (curve.type == GraphCurve::TextPlot) {
			stmp.str(cvehd.substr(0x13));
			GET_SHORT(stmp, curve.text.rotation)
			curve.text.rotation /= 10;
			GET_SHORT(stmp, curve.text.fontSize)

			h = cvehd[0x19];
			switch (h) {
				case 26:
					curve.text.justify = TextProperties::Center;
					break;
				case 2:
					curve.text.justify = TextProperties::Right;
					break;
				default:
					curve.text.justify = TextProperties::Left;
					break;
			}

			h = cvehd[0x20];
			curve.text.fontUnderline = (h & 0x1);
			curve.text.fontItalic = (h & 0x2);
			curve.text.fontBold = (h & 0x8);
			curve.text.whiteOut = (h & 0x20);

			char offset = cvehd[0x37];
			curve.text.xOffset = offset * 5;
			offset = cvehd[0x38];
			curve.text.yOffset = offset * 5;
		}

		//vector
		if (curve.type == GraphCurve::FlowVector || curve.type == GraphCurve::Vector) {
			stmp.str(cvehd.substr(0x56));
			GET_FLOAT(stmp, curve.vector.multiplier)

			h = cvehd[0x5E];
			column = findDataByIndex(nColY - 1 + h - 0x64);
			if (column.first.size() > 0)
				curve.vector.endXColumnName = column.second;

			h = cvehd[0x62];
			column = findDataByIndex(nColY - 1 + h - 0x64);
			if (column.first.size() > 0)
				curve.vector.endYColumnName = column.second;

			h = cvehd[0x18];
			if (h >= 0x64) {
				column = findDataByIndex(nColY - 1 + h - 0x64);
				if (column.first.size() > 0)
					curve.vector.angleColumnName = column.second;
			} else if (h <= 0x08)
				curve.vector.constAngle = 45*h;

			h = cvehd[0x19];
			if (h >= 0x64){
				column = findDataByIndex(nColY - 1 + h - 0x64);
				if (column.first.size() > 0)
					curve.vector.magnitudeColumnName = column.second;
			} else
				curve.vector.constMagnitude = (int)curve.symbolSize;

			stmp.str(cvehd.substr(0x66));
			GET_SHORT(stmp, curve.vector.arrowLenght)
			curve.vector.arrowAngle = cvehd[0x68];

			h = cvehd[0x69];
			curve.vector.arrowClosed = !(h & 0x1);

			stmp.str(cvehd.substr(0x70));
			GET_SHORT(stmp, w)
			curve.vector.width = (double)w/500.0;

			h = cvehd[0x142];
			switch (h) {
				case 2:
					curve.vector.position = VectorProperties::Midpoint;
					break;
				case 4:
					curve.vector.position = VectorProperties::Head;
					break;
				default:
					curve.vector.position = VectorProperties::Tail;
					break;
			}
		}
		//pie
		if (curve.type == GraphCurve::Pie) {
			h = cvehd[0x92];

			curve.pie.formatPercentages = (h & 0x01);
			curve.pie.formatValues		= (h & 0x02);
			curve.pie.positionAssociate = (h & 0x08);
			curve.pie.clockwiseRotation = (h & 0x20);
			curve.pie.formatCategories	= (h & 0x80);

			curve.pie.formatAutomatic = cvehd[0x93];
			stmp.str(cvehd.substr(0x94));
			GET_SHORT(stmp, curve.pie.distance)
			curve.pie.viewAngle = cvehd[0x96];
			curve.pie.thickness = cvehd[0x98];

			stmp.str(cvehd.substr(0x9A));
			GET_SHORT(stmp, curve.pie.rotation)

			stmp.str(cvehd.substr(0x9E));
			GET_SHORT(stmp, curve.pie.displacement)

			stmp.str(cvehd.substr(0xA0));
			GET_SHORT(stmp, curve.pie.radius)
			GET_SHORT(stmp, curve.pie.horizontalOffset)

			stmp.str(cvehd.substr(0xA6));
			GET_INT(stmp, curve.pie.displacedSectionCount)
		}
		//surface
		if (glayer.isXYY3D || curve.type == GraphCurve::Mesh3D) {
			curve.surface.type = cvehd[0x17];
			h = cvehd[0x1C];
			if ((h & 0x60) == 0x60)
				curve.surface.grids = SurfaceProperties::X;
			else if (h & 0x20)
				curve.surface.grids = SurfaceProperties::Y;
			else if (h & 0x40)
				curve.surface.grids = SurfaceProperties::None;
			else
				curve.surface.grids = SurfaceProperties::XY;

			curve.surface.sideWallEnabled = (h & 0x10);
			curve.surface.frontColor = getColor(cvehd.substr(0x1D,4));

			stmp.str(cvehd.substr(0x14C));
			GET_SHORT(stmp, w)
			curve.surface.gridLineWidth = (double)w/500.0;

			curve.surface.gridColor = getColor(cvehd.substr(0x14E,4));

			h = cvehd[0x13];
			curve.surface.backColorEnabled = (h & 0x08);

			curve.surface.backColor = getColor(cvehd.substr(0x15A,4));
			curve.surface.xSideWallColor = getColor(cvehd.substr(0x15E,4));
			curve.surface.ySideWallColor = getColor(cvehd.substr(0x162,4));

			curve.surface.surface.fill = (h & 0x10);
			curve.surface.surface.contour = (h & 0x40);
			stmp.str(cvehd.substr(0x94));
			GET_SHORT(stmp, w)
			curve.surface.surface.lineWidth = (double)w/500.0;
			curve.surface.surface.lineColor = getColor(cvehd.substr(0x96,4));

			curve.surface.topContour.fill = (h & 0x02);
			curve.surface.topContour.contour = (h & 0x04);
			stmp.str(cvehd.substr(0xB4));
			GET_SHORT(stmp, w)
			curve.surface.topContour.lineWidth = (double)w/500.0;
			curve.surface.topContour.lineColor = getColor(cvehd.substr(0xB6,4));

			curve.surface.bottomContour.fill = (h & 0x80);
			curve.surface.bottomContour.contour = (h & 0x01);
			stmp.str(cvehd.substr(0xA4));
			GET_SHORT(stmp, w)
			curve.surface.bottomContour.lineWidth = (double)w/500.0;
			curve.surface.bottomContour.lineColor = getColor(cvehd.substr(0xA6,4));
		}
		if (curve.type == GraphCurve::Mesh3D || curve.type == GraphCurve::Contour || curve.type == GraphCurve::XYZContour) {
			if (curve.type == GraphCurve::Contour || curve.type == GraphCurve::XYZContour) glayer.isXYY3D = false;
			ColorMap& colorMap = (curve.type == GraphCurve::Mesh3D ? curve.surface.colorMap : curve.colorMap);
			h = cvehd[0x13];
			colorMap.fillEnabled = (h & 0x82);

			if (curve.type == GraphCurve::Contour) {
				stmp.str(cvehd.substr(0x7A));
				GET_SHORT(stmp, curve.text.fontSize)

				h = cvehd[0x83];
				curve.text.fontUnderline = (h & 0x1);
				curve.text.fontItalic = (h & 0x2);
				curve.text.fontBold = (h & 0x8);
				curve.text.whiteOut = (h & 0x20);

						file.seekg(2, ios_base::cur);
				curve.text.color = getColor(cvehd.substr(0x86,4));
			}
			// TODO: replace d_colormap_offset by appropiate hex value
			// file.seekg(LAYER + d_colormap_offset, ios_base::beg);
			// readColorMap(colorMap);
			cout << "readColorMap and d_colormap_offset not implemented" << endl;
		}

		curve.fillAreaColor = getColor(cvehd.substr(0xC2,4));
		stmp.str(cvehd.substr(0xC6));
		GET_SHORT(stmp, w)
		curve.fillAreaPatternWidth = (double)w/500.0;

		curve.fillAreaPatternColor = getColor(cvehd.substr(0xCA,4));
		curve.fillAreaPattern = cvehd[0xCE];
		curve.fillAreaPatternBorderStyle = cvehd[0xCF];
		stmp.str(cvehd.substr(0xD0));
		GET_SHORT(stmp, w)
		curve.fillAreaPatternBorderWidth = (double)w/500.0;
		curve.fillAreaPatternBorderColor = getColor(cvehd.substr(0xD2,4));

		curve.fillAreaTransparency = cvehd[0x11E];

		curve.lineColor = getColor(cvehd.substr(0x16A,4));

		if (curve.type != GraphCurve::Contour) curve.text.color = curve.lineColor;

		stmp.str(cvehd.substr(0x17));
		GET_SHORT(stmp, curve.symbolType)

		curve.symbolFillColor = getColor(cvehd.substr(0x12E,4));
		curve.symbolColor = getColor(cvehd.substr(0x132,4));
		curve.vector.color = curve.symbolColor;

		h = cvehd[0x136];
		curve.symbolThickness = (h == 255 ? 1 : h);
		curve.pointOffset = cvehd[0x137];
		h = cvehd[0x138];
		curve.symbolFillTransparency = cvehd[0x139];

		h = cvehd[0x143];
		curve.connectSymbols = (h&0x8);

	}
}
