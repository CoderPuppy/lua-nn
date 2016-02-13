package = "nn"
version = "scm-1"
source = {
	url = "git://github.com/CoderPuppy/lua-nn.git"
}
dependencies = {
	"lua >= 5.1 <= 5.3"
}
build = {
	type = "builtin",
	modules = {
		nn = {
			sources = {"lnn.c"},
			libraries = {"nanomsg"},
		}
	}
}
