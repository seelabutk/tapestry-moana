#!/usr/bin/env bash

tag=moana:$USER
xauth=/tmp/.docker.xauth.$USER
data=/mnt/seenas2/data/moana
port=8861
registry=accona.eecs.utk.edu:5000
name=moana_service
global=1

build() {
	docker build -t $tag .
}

run() {
	docker run \
		-it --rm \
		--net=host \
		-v $data:$data \
		$tag "$@"
}

push() {
	docker tag $tag $registry/$tag
	docker push $registry/$tag
}

create() {
	docker service create \
		--name $name \
		-p $port:$port \
		${global:+--mode global} \
		--mount type=bind,src=$data,dst=$data \
		$registry/$tag python3.7 -u server.py --port $port "$@"
}

destroy() {
	docker service rm $name
}

scale() {
	docker service scale $name=${1?:need a number}
}

logs() {
	docker service logs $name "$@"
}

inspect() {
	# Thanks https://stackoverflow.com/q/48235040
	rm -f $xauth
	xauth nlist $DISPLAY | sed -e 's/^..../ffff/' | xauth -f $xauth nmerge -
	docker run -it --rm --entrypoint bash -v $PWD:$PWD -v $data:$data -w $PWD -e DISPLAY -v /etc/group:/etc/group:ro -v $xauth:$xauth -e XAUTHORITY=$xauth -v /etc/passwd:/etc/passwd:ro -v /etc/shadow:/etc/shadow:ro -v /etc/sudoers.d:/etc/sudoers.d:ro -v /tmp/.X11-unix:/tmp/.X11-unix:rw --ipc=host --net=host $tag "$@"
}

extract() {
	tar xf moana-ospray-demo.tgz -C _ --strip-components 2
}

checkout() {
	local f
	f=${1?:Need file}

	if ! [ -e "$f" ]; then
		printf 'File should exist\n' >&2
		return 1
	fi

	if ! [ "${f%%/*}" = _ ]; then
		printf 'File should be in _ directory\n' >&2
		return 1
	fi

	mkdir -p new
	tar cf - -C _ "${f#*/}" | tar xf - -C new
}

server.py() {
	run python3.7 -u server.py --port $port "$@"
}

"$@"
