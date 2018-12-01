#pragma once

// Some notes:
// - createChild should simply create a STreeNode of the derived type, NOT set its parent
//   (via the constructor or otherwise)
// - Deleting a STreeNode will not remove it from its parent, this must be done manually

class STreeNode
{
public:
	STreeNode(STreeNode* parent);
	virtual ~STreeNode();

	void allowDup(bool dup) { allow_dup_child_ = dup; }
	bool allowDup() { return allow_dup_child_; }

	STreeNode*     parent() { return parent_; }
	virtual string name()            = 0;
	virtual void   setName(string name) = 0;
	virtual string path();

	unsigned                   nChildren() { return children_.size(); }
	STreeNode*                 child(unsigned index);
	virtual STreeNode*         child(string name);
	virtual vector<STreeNode*> children(string name);
	virtual void               addChild(STreeNode* child);
	virtual STreeNode*         addChild(string name);
	virtual bool               removeChild(STreeNode* child);
	const vector<STreeNode*>&  allChildren() const { return children_; }

	virtual bool isLeaf() { return children_.empty(); }

protected:
	vector<STreeNode*> children_;
	STreeNode*         parent_;
	bool               allow_dup_child_;

	virtual STreeNode* createChild(string name) = 0;
};
