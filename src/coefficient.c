struct QsCoefficient {
	unsigned size;
	char* expression;
}

QsCoefficient* qs_coefficient_new_from_binary( char* data,unsigned size ) {
	QsCoefficient* result = malloc( sizeof (QsCoefficient) );
	result->size = size;
	result->expression = malloc( size );
	memcpy( result->expression,data,size );
	return result;
}

QsCoefficient* qs_coefficient_cpy( const QsCoefficient* c ) {
	QsCoefficient* result = malloc( sizeof (QsCoefficient) );
	result->size = c->size;
	result->expression = malloc( c->size );
	memcpy( result->expression,c->expression,c->size );
	return result;
}
