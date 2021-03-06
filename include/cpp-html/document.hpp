#ifndef PUGIHTML_DOCUMENT_HPP
#define PUGIHTML_DOCUMENT_HPP 1

#include <vector>
#include <memory>

#include <cpp-html/cpp-html.hpp>
#include <cpp-html/node.hpp>


namespace cpphtml
{

/**
 * Document class (DOM tree root).
 */
class document : public node {
public:
	/**
	 * Builds an empty document. It's html node with type node_document.
	 */
	static std::shared_ptr<document> create();

	/**
	 * Returns an array of all the links in the current document.
	 * The links collection counts <a href=""> tags and <area> tags.
	 */
	std::vector<std::shared_ptr<node> > links() const;

	/**
	 * Traverses DOM tree and searches for html node with the specified
	 * id attribute. If no tag is found, empty html node is returned.
	 */
	std::shared_ptr<node> get_element_by_id(const string_type& id) const;

	/**
	 * @return a list of alls elements with the specified tag name.
	 */
	std::vector<std::shared_ptr<node> > get_elements_by_tag_name(
		const string_type& tag_name) const;

private:
	/**
	 * Builds an empty document. It's html node with type node_document.
	 */
	document();
};

} // cpp-html.

#endif /* PUGIHTML_DOCUMENT_HPP */
