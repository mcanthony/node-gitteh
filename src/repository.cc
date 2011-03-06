#include "repository.h"
#include "commit.h"
#include "tree.h"
#include "index.h"
#include "tag.h"
#include "rev_walker.h"
#include "rawobj.h"

namespace gitteh {

Persistent<FunctionTemplate> Repository::constructor_template;

void Repository::Init(Handle<Object> target) {
	HandleScope scope;

	Local<FunctionTemplate> t = FunctionTemplate::New(New);
	constructor_template = Persistent<FunctionTemplate>::New(t);
	constructor_template->SetClassName(String::New("Repository"));
	t->InstanceTemplate()->SetInternalFieldCount(1);

	NODE_SET_PROTOTYPE_METHOD(t, "getCommit", GetCommit);
	NODE_SET_PROTOTYPE_METHOD(t, "getTree", GetTree);
	NODE_SET_PROTOTYPE_METHOD(t, "getTag", GetTag);
	NODE_SET_PROTOTYPE_METHOD(t, "getRawObject", GetRawObject);
	NODE_SET_PROTOTYPE_METHOD(t, "createWalker", CreateWalker);

	NODE_SET_PROTOTYPE_METHOD(t, "createTag", CreateTag);
	NODE_SET_PROTOTYPE_METHOD(t, "createTree", CreateTree);
	NODE_SET_PROTOTYPE_METHOD(t, "createCommit", CreateCommit);

	NODE_SET_PROTOTYPE_METHOD(t, "exists", Exists);

	t->InstanceTemplate()->SetAccessor(String::New("index"), IndexGetter);

	target->Set(String::New("Repository"), t->GetFunction());
}

Handle<Value> Repository::New(const Arguments& args) {
	HandleScope scope;

	REQ_ARGS(1);
	REQ_STR_ARG(0, path);

	Repository *repo = new Repository();

	if(int result = git_repository_open(&repo->repo_, *path) != GIT_SUCCESS) {
		Handle<Value> ex = Exception::Error(String::New("Git error."));
		return ThrowException(ex);
	}

	repo->path_ = *path;

	args.This()->Set(String::New("path"), String::New(repo->path_), ReadOnly);

	repo->odb_ = git_repository_database(repo->repo_);

	repo->Wrap(args.This());
	return args.This();
}

Handle<Value> Repository::GetCommit(const Arguments& args) {
	HandleScope scope;

	REQ_ARGS(1);
	REQ_OID_ARG(0, commitOid);

	Repository *repo = ObjectWrap::Unwrap<Repository>(args.This());

	git_commit* commit;
	if(git_commit_lookup(&commit, repo->repo_, &commitOid) != GIT_SUCCESS) {
		// TODO: error code handling.
		return scope.Close(Null());
	}

	Commit *commitObject = repo->wrapCommit(commit);
	return scope.Close(commitObject->handle_);
}

Handle<Value> Repository::GetTree(const Arguments& args) {
	HandleScope scope;

	REQ_ARGS(1);
	REQ_OID_ARG(0, treeOid);

	Repository *repo = ObjectWrap::Unwrap<Repository>(args.This());

	git_tree *tree;
	if(git_tree_lookup(&tree, repo->repo_, &treeOid) != GIT_SUCCESS) {
		return scope.Close(Null());
	}

	Tree *treeObject = repo->wrapTree(tree);
	return scope.Close(treeObject->handle_);
}

Handle<Value> Repository::GetTag(const Arguments& args) {
	HandleScope scope;

	REQ_ARGS(1);
	REQ_OID_ARG(0, tagOid);

	Repository *repo = ObjectWrap::Unwrap<Repository>(args.This());

	git_tag *tag;
	int res = git_tag_lookup(&tag, repo->repo_, &tagOid);
	if(res != GIT_SUCCESS)
		THROW_GIT_ERROR("Couldn't get tag.", res);

	Tag *tagObj = repo->wrapTag(tag);
	return scope.Close(tagObj->handle_);
}

Handle<Value> Repository::GetRawObject(const Arguments& args) {
	HandleScope scope;

	Repository *repo = ObjectWrap::Unwrap<Repository>(args.This());

	REQ_ARGS(1);
	REQ_OID_ARG(0, oid);

	git_rawobj obj;
	if(git_odb_read(&obj, repo->odb_, &oid) == GIT_ENOTFOUND) {
		return Null();
	}

	Local<Value> arg = External::New(&obj);
	Persistent<Object> result(RawObject::constructor_template->GetFunction()->NewInstance(1, &arg));
	return scope.Close(result);
}

Handle<Value> Repository::CreateWalker(const Arguments& args) {
	HandleScope scope;

	Repository *repo = ObjectWrap::Unwrap<Repository>(args.This());

	git_revwalk *walker;
	int res = git_revwalk_new(&walker, repo->repo_);
	if(res != GIT_SUCCESS) {
		THROW_GIT_ERROR("Couldn't create revision walker", res);
	}

	Handle<Value> constructorArgs[2] = { External::New(walker), External::New(repo) };
	Handle<Object> instance = RevWalker::constructor_template->GetFunction()->NewInstance(2, constructorArgs);
	
	RevWalker *walkerObject = ObjectWrap::Unwrap<RevWalker>(instance);
	return scope.Close(walkerObject->handle_);
}

Handle<Value> Repository::IndexGetter(Local<String>, const AccessorInfo& info) {
	HandleScope scope;

	Repository *repo = ObjectWrap::Unwrap<Repository>(info.This());
	if(repo->index_ == NULL) {
		git_index *index;
		int result = git_repository_index(&index, repo->repo_);
		if(result == GIT_EBAREINDEX) {
			git_index_open_bare(&index, repo->path_);
		}

		Handle<Value> arg = External::New(index);
		Handle<Object> instance = Index::constructor_template->GetFunction()->NewInstance(1, &arg);
		repo->index_ = ObjectWrap::Unwrap<Index>(instance);
	}

	return repo->index_->handle_;
}

Handle<Value> Repository::CreateTag(const Arguments& args) {
	HandleScope scope;
	Repository *repo = ObjectWrap::Unwrap<Repository>(args.This());

	git_tag *tag;
	int res = git_tag_new(&tag, repo->repo_);
	if(res != GIT_SUCCESS)
		THROW_GIT_ERROR("Couldn't create new tag.", res);

	Tag *tagObject = repo->wrapTag(tag);
	return scope.Close(tagObject->handle_);
}

Handle<Value> Repository::CreateTree(const Arguments& args) {
	HandleScope scope;

	Repository *repo = ObjectWrap::Unwrap<Repository>(args.This());

	git_tree *tree;
	int res = git_tree_new(&tree, repo->repo_);
	if(res != GIT_SUCCESS)
		THROW_GIT_ERROR("Couldn't create tree.", res);

	Tree *treeObject = repo->wrapTree(tree);
	return treeObject->handle_;
}

Handle<Value> Repository::CreateCommit(const Arguments& args) {
	HandleScope scope;

	Repository *repo = ObjectWrap::Unwrap<Repository>(args.This());

	git_commit *commit;
	int result = git_commit_new(&commit, repo->repo_);

	if(result != GIT_SUCCESS) {
		// TODO: error handling.
		return Null();
	}

	Commit *commitObject = repo->wrapCommit(commit);
	return scope.Close(commitObject->handle_);
}

Handle<Value> Repository::Exists(const Arguments& args) {
	HandleScope scope;
	Repository *repo = ObjectWrap::Unwrap<Repository>(args.This());

	REQ_ARGS(1);
	REQ_OID_ARG(0, objOid);

	return Boolean::New(git_odb_exists(repo->odb_, &objOid));
}

Repository::~Repository() {
	close();
}

void Repository::close() {
	if(repo_) {
		git_repository_free(repo_);
		repo_ = NULL;
	}
}

Commit *Repository::wrapCommit(git_commit *commit) {
	Commit *commitObject;
	if(commitStore_.getObjectFor(commit, &commitObject)) {
		// Commit needs to know who it's daddy is.
		commitObject->repository_ = this;
	}

	return commitObject;
}

Tree *Repository::wrapTree(git_tree *tree) {
	Tree *treeObject;
	if(treeStore_.getObjectFor(tree, &treeObject)) {
		treeObject->repository_ = this;
	}

	return treeObject;
}

Tag *Repository::wrapTag(git_tag *tag) {
	Tag *tagObject;
	if(tagStore_.getObjectFor(tag, &tagObject)) {
		tagObject->repository_ = this;
	}

	return tagObject;
}

} // namespace gitteh