#include <cassert>
#include <cstring>
#include <stdexcept>
#include <algorithm>
#include <list>
#include <locale>
#include <sstream>
#include <unordered_map>
#include <set>

#include <cpp-html/attribute.hpp>
#include <cpp-html/node.hpp>
#include <cpp-html/document.hpp>
#include <cpp-html/parser.hpp>


namespace cpphtml
{

/**
 * This table maps ASCII symbols with their possible types in enum chartype_t.
 */
const unsigned char chartype_table[256] = {
	55,  0,   0,   0,   0,   0,   0,   0,      0,   12,  12,  0,   0,   62,  0,   0,   // 0-15
	0,   0,   0,   0,   0,   0,   0,   0,      0,   0,   0,   0,   0,   0,   0,   0,   // 16-31
	10,   0,   4,   0,   0,   0,   4,   4,      0,   0,   0,   0,   0,   96,  64,  0,   // 32-47
	64,  64,  64,  64,  64,  64,  64,  64,     64,  64,  192, 0,   1,   0,   50,  0,   // 48-63
	0,   192, 192, 192, 192, 192, 192, 192,    192, 192, 192, 192, 192, 192, 192, 192, // 64-79
	192, 192, 192, 192, 192, 192, 192, 192,    192, 192, 192, 0,   0,   16,  0,   192, // 80-95
	0,   192, 192, 192, 192, 192, 192, 192,    192, 192, 192, 192, 192, 192, 192, 192, // 96-111
	192, 192, 192, 192, 192, 192, 192, 192,    192, 192, 192, 0, 0, 0, 0, 0,           // 112-127

	192, 192, 192, 192, 192, 192, 192, 192,    192, 192, 192, 192, 192, 192, 192, 192, // 128+
	192, 192, 192, 192, 192, 192, 192, 192,    192, 192, 192, 192, 192, 192, 192, 192,
	192, 192, 192, 192, 192, 192, 192, 192,    192, 192, 192, 192, 192, 192, 192, 192,
	192, 192, 192, 192, 192, 192, 192, 192,    192, 192, 192, 192, 192, 192, 192, 192,
	192, 192, 192, 192, 192, 192, 192, 192,    192, 192, 192, 192, 192, 192, 192, 192,
	192, 192, 192, 192, 192, 192, 192, 192,    192, 192, 192, 192, 192, 192, 192, 192,
	192, 192, 192, 192, 192, 192, 192, 192,    192, 192, 192, 192, 192, 192, 192, 192,
	192, 192, 192, 192, 192, 192, 192, 192,    192, 192, 192, 192, 192, 192, 192, 192
};


inline bool
is_chartype(char_type ch, enum chartype_t char_type)
{
#ifdef PUGIHTML_WCHAR_MODE
	return (static_cast<unsigned int>(ch) < 128
		? chartype_table[static_cast<unsigned int>(ch)]
		: chartype_table[128]) & (char_type);
#else
	return chartype_table[static_cast<unsigned char>(ch)] & (char_type);
#endif
}

// Parser utilities.
// TODO(povilas): replace with inline functions or completely remove some of them.

inline const char_type*
skip_white_spaces(const char_type* str)
{
	while (is_chartype(*str, ct_space)) {
		++str;
	}

	return str;
}

#define THROW_ERROR(err, m) (void)m, throw parse_error(err)

#define SCANFOR(X)			{ while (*s != 0 && !(X)) ++s; }
#define SCANWHILE(X)		{ while ((X)) ++s; }
#define CHECK_ERROR(err, m)	{ if (*s == 0) THROW_ERROR(err, m); }

// Utility macro for last character handling
#define ENDSWITH(c, e) ((c) == (e) || ((c) == 0 && endch == (e)))


// TODO(povilas): replace with set for faster search.
std::list<string_type> html_void_elements = {"AREA", "BASE", "BR",
	"COL", "EMBED", "HR", "IMG", "INPUT", "KEYGEN", "LINK", "MENUITEM",
	"META", "PARAM", "SOURCE", "TRACK", "WBR"};


// End tag might be ommited for some elements, if they are followed by specific
// elements. E.g. one might write
//	<ul>
//		<li>item1
//		<li>item2
//	</ul>
std::unordered_map<string_type, std::set<string_type> > no_end_tag_by_sibling = {
	{"LI", {"LI"}},
	{"TD", {"TD"}},
	{"TR", {"TR", "TD"}},
	{"TH", {"TH"}},
	{"TBODY", {"THEAD"}},
	{"DD", {"DD", "DT"}},
	{"DT", {"DT"}}
};


// End tag might be ommited for some elements, if they are the last child
// of their parent element. E.g.
//	<ul>
//		<li>item1
//	</ul>
std::set<string_type> no_end_tag_by_child = {"LI", "P", "TD", "TR", "TBODY",
	"THEAD", "TH", "DD"};


/**
 * Checks if previous sibling should be closed automatically.
 */
inline bool
autoclose_prev_sibling(const std::string& tag_name,
	const std::string& prev_sibling_name)
{
	auto it_siblings = no_end_tag_by_sibling.find(tag_name);
	if (it_siblings == std::end(no_end_tag_by_sibling)) {
		return false;
	}

	auto it = it_siblings->second.find(prev_sibling_name);
	return it != std::end(it_siblings->second);
}


inline bool
autoclose_last_child(const std::string& tag_name)
{
	return no_end_tag_by_child.find(tag_name)
		!= std::end(no_end_tag_by_child);
}


inline bool
is_void_element(const std::string& tag_name)
{
	auto it = std::find(std::begin(html_void_elements),
		std::end(html_void_elements), tag_name);
	return it != std::end(html_void_elements);
}


const char_type*
parser::advance_doctype_primitive(const char_type* s)
{
	// Quoted string.
	if (*s == '"' || *s == '\'') {
		char_type ch = *s++;
		SCANFOR(*s == ch);
		if (!*s) THROW_ERROR(status_bad_doctype, s);

		++s;
	}
	// <? ... ?>
	else if (s[0] == '<' && s[1] == '?') {
		s += 2;
		// no need for ENDSWITH because ?> can't terminate proper doctype
		SCANFOR(s[0] == '?' && s[1] == '>');
		if (!*s) THROW_ERROR(status_bad_doctype, s);

		s += 2;
	}
	// <-- ... -->
	else if (s[0] == '<' && s[1] == '!' && s[2] == '-' && s[3] == '-') {
		s += 4;
		// no need for ENDSWITH because --> can't terminate proper doctype
		SCANFOR(s[0] == '-' && s[1] == '-' && s[2] == '>');
		if (!*s) THROW_ERROR(status_bad_doctype, s);

		s += 4;
	}
	else {
		THROW_ERROR(status_bad_doctype, s);
	}

	return s;
}


const char_type*
parser::advance_doctype_ignore(const char_type* s)
{
	assert(s[0] == '<' && s[1] == '!' && s[2] == '[');
	++s;

	while (*s) {
		if (s[0] == '<' && s[1] == '!' && s[2] == '[') {
			// Nested ignore section.
			s = advance_doctype_ignore(s);
		}
		else if (s[0] == ']' && s[1] == ']' && s[2] == '>') {
			// Ignore section end.
			return s + 3;
		}
		else {
			++s;
		}
	}

	THROW_ERROR(status_bad_doctype, s);

	return s;
}


const char_type*
parser::advance_doctype_group(const char_type* s, char_type endch,
	bool top_level)
{
	assert(s[0] == '<' && s[1] == '!');
	++s;

	while (*s) {
		if (s[0] == '<' && s[1] == '!') {
			if (s[2] == '[') {
				// Ignore.
				s = advance_doctype_ignore(s);
			}
			else {
				// Some control group.
				s = advance_doctype_group(s, endch, false);
			}
		}
		else if (s[0] == '<' || s[0] == '"' || s[0] == '\'') {
			// unknown tag (forbidden), or some primitive group
			s = advance_doctype_primitive(s);
		}
		else if (*s == '>') {
			return ++s;
		}
		else {
			++s;
		}
	}

	if (!top_level || endch != '>') {
		THROW_ERROR(status_bad_doctype, s);
	}

	return s;
}


parser::parser(unsigned int options) : options_(options),
	document_(document::create()), current_node_(document_)
{
}


const char_type*
parser::parse_exclamation(const char_type* s, char_type endch)
{
	// Skip '<!'.
	s += 2;

	// '<!-...' - comment.
	if (*s == '-') {
		++s;
		if (*s != '-') {
			THROW_ERROR(status_bad_comment, s);
		}

		++s;
		const char_type* comment_start = s;

		// Scan for terminating '-->'.
		SCANFOR(s[0] == '-' && s[1] == '-'
			&& ENDSWITH(s[2], '>'));
		CHECK_ERROR(status_bad_comment, s);

		if (this->option_set(parse_comments)) {
			// TODO(povilas): if this->option_set(parse_eol),
			// replace \r\n to \n.
			size_t comment_len = (s - 1) - comment_start + 1;
			string_type comment(comment_start, comment_len);

			auto comment_node = node::create(node_comment);
			comment_node->value(comment);

			this->current_node_->append_child(comment_node);
		}

		// Step over the '\0->'.
		s += (s[2] == '>' ? 3 : 2);
	}
	// '<![CDATA[...'
	else if (*s == '[') {
		if (!(*++s=='C' && *++s=='D' && *++s=='A' && *++s=='T'
			&& *++s=='A' && *++s == '[')) {
			THROW_ERROR(status_bad_cdata, s);
		}

		++s;
		const char_type* cdata_start = s;

		SCANFOR(s[0] == ']' && s[1] == ']'
			&& ENDSWITH(s[2], '>'));
		CHECK_ERROR(status_bad_cdata, s);

		if (this->option_set(parse_cdata)) {
			// TODO(povilas): if this->option_set(parse_eol),
			// replace \r\n to \n.
			size_t cdata_len = s - cdata_start + 1;
			string_type cdata(cdata_start, cdata_len);

			auto node = node::create(node_cdata);
			node->value(cdata);
			this->current_node_->append_child(node);
		}

		++s;
		s += (s[1] == '>' ? 2 : 1); // Step over the last ']>'.
	}
	// <!DOCTYPE
	else if (s[0] == 'D' && s[1] == 'O' && s[2] == 'C' && s[3] == 'T'
		&& s[4] == 'Y' && s[5] == 'P' && ENDSWITH(s[6], 'E')) {
		s -= 2;

		const char_type* doctype_start = s + 9;

		s = advance_doctype_group(s, endch);

		if (this->option_set(parse_doctype)) {
			while (is_chartype(*doctype_start, ct_space)) {
				++doctype_start;
			}

			assert(s[-1] == '>');
			size_t doctype_len = (s - 2) - doctype_start + 1;
			string_type doctype(doctype_start, doctype_len);

			auto node = node::create(node_doctype);
			node->value(doctype);
			this->current_node_->append_child(node);
		}
	}
	else if (*s == 0 && endch == '-') THROW_ERROR(status_bad_comment, s);
	else if (*s == 0 && endch == '[') THROW_ERROR(status_bad_cdata, s);
	else THROW_ERROR(status_unrecognized_tag, s);

	return s;
}


inline void
str_toupper(string_type& str)
{
	std::locale loc;
	for (auto it = std::begin(str); it != std::end(str); ++it) {
		*it = std::toupper(*it, loc);
	}
}


std::shared_ptr<node>
find_parent_node_for_new_tag(std::shared_ptr<node> current_node,
	const string_type& new_tag_name)
{
	auto new_tag_parent = current_node;

	auto parent = current_node->parent();
	if (parent && autoclose_prev_sibling(new_tag_name,
		current_node->name())) {

		while (parent->parent() && autoclose_prev_sibling(new_tag_name,
			parent->name())) {
			parent = parent->parent();
		}

		new_tag_parent = parent;
	}

	return new_tag_parent;
}


std::shared_ptr<document>
parser::parse(const string_type& str_html)
{
	this->status_ = status_ok;

	if (str_html.size() == 0) {
		return this->document_;
	}

	const char_type* s = str_html.c_str();

	// Flag indicating if last parsed tag is void html element.
	bool last_element_void = false;

	auto on_tag_start = [&](const std::string& tag_name) {
		if (last_element_void) {
			this->current_node_ = this->current_node_->parent();
			last_element_void = false;
		}

		auto node = node::create(node_element);
		node->name(tag_name);

		auto new_tag_parent = find_parent_node_for_new_tag(
			this->current_node_, tag_name);
		new_tag_parent->append_child(node);

		this->current_node_ = node;
	};

	auto on_closing_tag = [&](const std::string& tag_name) {
		if (tag_name != this->current_node_->name()
			&& (autoclose_last_child(this->current_node_->name())
			|| last_element_void)) {

			if (last_element_void) {
				this->current_node_ = this->current_node_->parent();
			}
			else while (autoclose_last_child(
				this->current_node_->name())) {
				this->current_node_ = this->current_node_->parent();
			}

			last_element_void = false;
		}

		const string_type& expected_name = this->current_node_->name();
		if (expected_name != tag_name) {
			std::string err_msg = "Expected: '" + expected_name
				+ "', found: '" + tag_name + "'";
			throw parse_error(status_end_element_mismatch, str_html,
				s, err_msg);
		}

		if (this->current_node_->parent()) {
			this->current_node_ = this->current_node_->parent();
			last_element_void = false;
		}
	};

	auto on_pcdata = [&](const std::string& pcdata) {
		if (last_element_void) {
			this->current_node_ = this->current_node_->parent();
			last_element_void = false;
		}

		auto node = node::create(node_cdata);
		node->value(pcdata);
		this->current_node_->append_child(node);
	};

	auto on_attribute = [&](const std::string& attr_name,
		const std::string& attr_val) {
		auto attr = attribute::create(attr_name, attr_val);
		this->current_node_->append_attribute(attr);
	};

	auto on_script = [&](const string_type& script_value) {
		auto node = node::create(node_cdata);
		node->value(script_value);
		this->current_node_->append_child(node);
	};

	auto parse_pcdata = [&]() {
		const char_type* pcdata_start = s;
		while (!is_chartype(*s, ct_parse_pcdata)) {
			++s;
		}

		size_t pcdata_len = (s - 1) - pcdata_start + 1;
		string_type pcdata = string_type(pcdata_start,
			pcdata_len);

		on_pcdata(pcdata);
	};

	auto parse_script = [&]() {
		const char_type* script_value_start = s;
		const char_type* script_value_end = s;

		while (*s != '\0') {
			if (strncmp(s, "</script>", 9) == 0) {
				script_value_end = s - 1;
				break;
			}
			else {
				++s;
			}
		}

		size_t script_value_len = script_value_end - script_value_start
			+ 1;
		string_type script_value = string_type(script_value_start,
			script_value_len);

		on_script(script_value);
	};

	auto on_attribute_name_state = [&]() {
		const char_type* attr_name_start = s;

		SCANWHILE(is_chartype(*s, ct_symbol));
		if (*s == '\0') {
			throw parse_error(status_bad_attribute, str_html, s);
		}

		size_t attr_name_len = (s - 1) - attr_name_start + 1;
		string_type attr_name = string_type(attr_name_start, attr_name_len);
		str_toupper(attr_name);

		s = skip_white_spaces(s);
		if (*s == '\0') {
			throw parse_error(status_bad_attribute, str_html, s);
		}

		string_type attr_val;
		// Attribute with value.
		if (*s == '=') {
			++s;
			s = skip_white_spaces(s);

			char_type quote_symbol = 0;
			if (*s == '"' || *s == '\'') {
				quote_symbol = *s;
				++s;
			}

			const char_type* attr_val_start = s;
			if (quote_symbol) {
				while (*s && *s != quote_symbol) {
					++s;
				}

				if (*s != quote_symbol) {
					throw parse_error(status_bad_attribute, str_html, s,
						"Bad attribute value closing symbol.");
				}
			}
			else {
				while (!is_chartype(*s, ct_parse_attr)) {
					++s;
				}
			}

			size_t attr_val_len = (s - 1) - attr_val_start + 1;
			attr_val = string_type(attr_val_start, attr_val_len);

			if (quote_symbol) {
				// Step over attribute value stop symbol.
				++s;
			}
			else {
				s = skip_white_spaces(s);
			}
		}
		// Attribute has no value.
		else {
			s = skip_white_spaces(s);
			if (*s == '\0') {
				throw parse_error(status_bad_attribute, str_html, s);
			}
		}

		on_attribute(attr_name, attr_val);
	};

	auto on_self_closing_start_tag_state = [&]() {
		++s;

		if (*s != '>') {
			throw parse_error(status_bad_start_element, str_html, s);
		}
		else {
			last_element_void = true;
		}
	};

	auto on_tag_open_state = [&]() {
		++s;

		// Check if the current character is a tag start symbol.
		if (is_chartype(*s, ct_start_symbol)) {
			const char_type* tag_name_start = s;

			// Scan while the current character is a symbol belonging
			// to the set of symbols acceptable within a tag. In other
			// words, scan until the termination symbol is discovered.
			SCANWHILE(is_chartype(*s, ct_symbol));

			size_t tag_name_len = (s - 1) - tag_name_start
				+ 1;
			string_type tag_name = string_type(
				tag_name_start, tag_name_len);
			str_toupper(tag_name);

			on_tag_start(tag_name);

			// End of tag.
			if (*s == '>') {
				last_element_void = is_void_element(this->current_node_->name());
			}
			else if (is_chartype(*s, ct_space)) {
				while (true) {
					s = skip_white_spaces(s);

					// Attribute start.
					if (is_chartype(*s, ct_start_symbol)) {
						on_attribute_name_state();
					}
					// Void element end.
					else if (*s == '/') {
						on_self_closing_start_tag_state();
						break;
					}
					// Tag end, also might be void element.
					else if (*s == '>') {
						last_element_void = is_void_element(this->current_node_->name());
						break;
					}
					else {
						throw parse_error(status_bad_start_element, str_html, s);
					}
				} // while
			}
			// Void HTML element.
			else if (*s == '/') {
				on_self_closing_start_tag_state();
			}
			else {
				throw parse_error(status_bad_start_element, str_html, s);
			}

			++s;
		}
		// Closing tag, e.g. </hmtl>
		else if (*s == '/') {
			++s;

			const char_type* tag_name_start = s;
			while (is_chartype(*s, ct_symbol)) {
				++s;
			}

			size_t tag_name_len = (s - 1) - tag_name_start
				+ 1;
			string_type tag_name = string_type(tag_name_start,
				tag_name_len);
			str_toupper(tag_name);

			on_closing_tag(tag_name);

			s = skip_white_spaces(s);
			if (*s != '>') {
				THROW_ERROR(status_bad_end_element, "");
			}

			++s;
		}
		// Comment: <!-- ...
		else if (*s == '!') {
			s = parse_exclamation(s - 1);
		}
		else {
			throw parse_error(status_unrecognized_tag,
				str_html, s);
		}
	};

	this->current_node_ = this->document_;

	// Parse while the current character is not '\0'.
	while (*s != '\0') {
		// Check if the current character is the start tag character
		if (*s == '<') {
			on_tag_open_state();
		}
		else {
			if (this->current_node_->name() == "SCRIPT") {
				parse_script();
			}
			else {
				parse_pcdata();
			}
		}
	}

	return this->document_;
}


string_type
parser::status_description() const
{
	return parser::status_description(this->status_);
}


string_type
parser::status_description(parse_status status)
{
	switch (status)
	{
	case status_ok:
		return "No error.";

	case status_file_not_found:
		return "File was not found.";

	case status_io_error:
		return "Error reading from file/stream.";

	case status_out_of_memory:
		return "Could not allocate memory.";

	case status_internal_error:
		return "Internal error occurred.";

	case status_unrecognized_tag:
		return "Could not determine tag type.";

	case status_bad_pi:
		return "Error parsing document declaration/processing "
			"instruction.";

	case status_bad_comment:
		return "Error parsing comment.";

	case status_bad_cdata:
		return "Error parsing CDATA section.";

	case status_bad_doctype:
		return "Error parsing document type declaration.";

	case status_bad_pcdata:
		return "Error parsing PCDATA section.";

	case status_bad_start_element:
		return "Error parsing start element tag.";

	case status_bad_attribute:
		return "Error parsing element attribute.";

	case status_bad_end_element:
		return "Error parsing end element tag.";

	case status_end_element_mismatch:
		return "Start-end tags mismatch.";

	default:
		return "Unknown error.";
	}
}


std::shared_ptr<document>
parser::get_document() const
{
	return this->document_;
}


// parse_error

parse_error::parse_error(parse_status status)
	: std::runtime_error(parser::status_description(status)),
	status_(status)
{
}


parse_error::parse_error(parse_status status, const std::string& str_html,
	const char* parse_pos, const std::string& err_msg) : std::runtime_error(
	parse_error::format_error_msg(status, str_html, parse_pos, err_msg)),
	status_(status)
{
}

parse_status
parse_error::status() const
{
	return this->status_;
}


std::string
parse_error::format_error_msg(parse_status status, const std::string& html,
	const char* pos, const std::string& err_msg)
{
	size_t line_nr = 0;

	auto find_newline = [&](const char* start) {
		return std::find_if(start, pos, [](char symb) {
			return symb == '\n';
		});
	};

	const char* str_html = html.c_str();

	auto it = find_newline(str_html);
	auto last_newline = str_html;
	while (it < pos) {
		last_newline = it;
		++line_nr;
		it = find_newline(it + 1);
	}

	size_t row_nr = pos - last_newline;

	size_t chars_to_print = str_html + html.size() - pos >= 20 ? 20
		: str_html + html.size() - pos;
	std::stringstream ss;
	ss << parser::status_description(status)
		<< " Line: " << line_nr << ", column: " << row_nr << ": '" <<
		std::string(pos, chars_to_print) << "...'. " << err_msg;
	return ss.str();
}



// Private methods.

bool
parser::option_set(unsigned int opt)
{
	return this->options_ & opt;
}

} // cpp-html.
