docker run -i --rm -v $PWD/..:/app -w /app $(docker build -q .) bash -c "cd tests/build; make; ./test_contactors; ./test_low_voltage; ./test_soc; ./test_events"

